"""MQTT -> validate -> enrich -> ML-score -> DB writer."""
from __future__ import annotations

import asyncio
import signal
from datetime import datetime, timedelta, timezone
from pathlib import Path

import aiomqtt
import orjson
from aiohttp import web
from pydantic import ValidationError

from iot_pdm.logging import get_logger
from iot_pdm.ml.anomaly import FEATURE_COLS, AnomalyDetector, RollingZScore
from iot_pdm.schemas.telemetry import BurstMessage, StatusMessage, TelemetryMessage
from iot_pdm.settings import get_settings
from iot_pdm.storage.db import (
    db,
    insert_dlq,
    insert_feature,
    recent_rms_values,
    upsert_device_status,
)

log = get_logger("worker")

counters: dict[str, int] = {
    "msgs_received": 0,
    "msgs_persisted": 0,
    "msgs_dlq": 0,
    "msgs_late": 0,
    "anomalies_detected": 0,
    "status_online": 0,
    "status_offline": 0,
    "db_retries": 0,
}

shutdown_event = asyncio.Event()
anomaly_detector: AnomalyDetector | None = None
zscore = RollingZScore(window_seconds=300)
mqtt_client_ref: aiomqtt.Client | None = None


async def handle_features(topic: str, payload: bytes) -> None:
    counters["msgs_received"] += 1

    try:
        data = orjson.loads(payload)
    except orjson.JSONDecodeError as e:
        await _to_dlq(topic, reason="malformed", payload=payload, error=str(e))
        return

    try:
        msg = TelemetryMessage.model_validate(data)
    except ValidationError as e:
        await _to_dlq(
            topic,
            reason="schema",
            payload=payload,
            device_id=data.get("device_id") if isinstance(data, dict) else None,
            error=e.errors(include_url=False)[0].get("msg") if e.errors() else "schema",
        )
        return

    settings = get_settings()
    now = datetime.now(timezone.utc)
    late = (now - msg.timestamp) > timedelta(seconds=settings.worker_late_tolerance_s)
    if late:
        counters["msgs_late"] += 1
        log.warning(
            "msg.late",
            msg_id=str(msg.msg_id),
            delay_s=(now - msg.timestamp).total_seconds(),
        )

    z = zscore.update(msg.device_id, msg.metrics.rms_g)

    features = {}
    for c in FEATURE_COLS:
        if c.startswith("mel_"):
            features[c] = msg.metrics.mel[int(c.split("_")[1])]
        else:
            features[c] = getattr(msg.metrics, c)

    anomaly_score: float | None = None
    anomaly_flag: bool | None = None
    if anomaly_detector and anomaly_detector.is_ready():
        anomaly_score, anomaly_flag = anomaly_detector.score(features)
        if anomaly_flag:
            counters["anomalies_detected"] += 1

    row = {
        "time": msg.timestamp,
        "device_id": msg.device_id,
        "site": msg.site,
        "asset": msg.asset,
        "msg_id": msg.msg_id,
        "seq": msg.seq,
        "quality": msg.quality,
        "rms_g": msg.metrics.rms_g,
        "kurtosis": msg.metrics.kurtosis,
        "crest": msg.metrics.crest,
        "peak_hz": msg.metrics.peak_hz,
        "peak_mag": msg.metrics.peak_mag,
        "mel": msg.metrics.mel,
        "therm_min_c": msg.metrics.therm_min_c,
        "therm_mean_c": msg.metrics.therm_mean_c,
        "therm_max_c": msg.metrics.therm_max_c,
        "anomaly_score": anomaly_score,
        "anomaly_flag": anomaly_flag,
        "z_rms": z,
        "late": late,
    }
    try:
        async with db.acquire() as conn:
            await insert_feature(conn, row)
        counters["msgs_persisted"] += 1
    except Exception as e:
        counters["db_retries"] += 1
        log.error("db.insert_failed", error=str(e), msg_id=str(msg.msg_id))
        await _to_dlq(
            topic,
            reason="db_error",
            payload=payload,
            device_id=msg.device_id,
            error=str(e),
        )
        return

    if anomaly_flag:
        await _publish_alert(msg.device_id, msg.site, msg.asset, anomaly_score)


async def handle_status(topic: str, payload: bytes) -> None:
    try:
        msg = StatusMessage.model_validate_json(payload)
    except ValidationError as e:
        await _to_dlq(topic, reason="schema", payload=payload, error=str(e))
        return
    parts = topic.split("/")
    device_id = parts[3] if len(parts) >= 5 else "unknown"

    if msg.status == "online":
        counters["status_online"] += 1
    else:
        counters["status_offline"] += 1

    async with db.acquire() as conn:
        await upsert_device_status(conn, device_id=device_id, status=msg.status, fw=msg.fw)
    log.info("device.status", device_id=device_id, status=msg.status)


async def handle_burst(topic: str, payload: bytes) -> None:
    try:
        msg = BurstMessage.model_validate_json(payload)
    except ValidationError as e:
        await _to_dlq(topic, reason="schema", payload=payload, error=str(e))
        return
    async with db.acquire() as conn:
        await conn.execute(
            """INSERT INTO burst_windows (time, device_id, axis, sample_rate_hz, samples_g, msg_id)
               VALUES ($1, $2, $3, $4, $5, $6)
               ON CONFLICT (msg_id, time) DO NOTHING""",
            msg.timestamp, msg.device_id, msg.axis, msg.sample_rate_hz,
            msg.samples_g, msg.msg_id,
        )


async def _to_dlq(
    topic: str,
    *,
    reason: str,
    payload: bytes,
    device_id: str | None = None,
    error: str | None = None,
) -> None:
    counters["msgs_dlq"] += 1
    text = payload.decode(errors="replace")[:8000]
    log.warning("msg.dlq", reason=reason, topic=topic, device_id=device_id, error=error)

    try:
        async with db.acquire() as conn:
            await insert_dlq(
                conn,
                topic=topic,
                reason=reason,
                payload=text,
                device_id=device_id,
                error=error,
            )
    except Exception as e:
        log.error("dlq.db_insert_failed", error=str(e))

    if mqtt_client_ref:
        dlq_topic = topic.rsplit("/", 1)[0] + "/dlq"
        try:
            await mqtt_client_ref.publish(
                dlq_topic,
                orjson.dumps({
                    "reason": reason,
                    "error": error or "",
                    "ts": datetime.now(timezone.utc).isoformat(),
                }),
                qos=1,
            )
        except Exception:
            pass


async def _publish_alert(
    device_id: str, site: str, asset: str, score: float | None
) -> None:
    if not mqtt_client_ref:
        return
    alert_topic = f"iotpdm/{site}/{asset}/{device_id}/alert"
    await mqtt_client_ref.publish(
        alert_topic,
        orjson.dumps({
            "anomaly": True,
            "score": score,
            "ts": datetime.now(timezone.utc).isoformat(),
        }),
        qos=1,
    )


async def route_message(msg: aiomqtt.Message) -> None:
    topic = str(msg.topic)
    payload = bytes(msg.payload) if msg.payload else b""
    if topic.endswith("/features"):
        await handle_features(topic, payload)
    elif topic.endswith("/status"):
        await handle_status(topic, payload)
    elif topic.endswith("/burst"):
        await handle_burst(topic, payload)
    else:
        log.debug("msg.unhandled_topic", topic=topic)


async def mqtt_loop() -> None:
    global mqtt_client_ref
    s = get_settings()
    interval = 2.0
    while not shutdown_event.is_set():
        try:
            async with aiomqtt.Client(
                hostname=s.mqtt_host,
                port=s.mqtt_port,
                username=s.mqtt_user,
                password=s.mqtt_pass,
                identifier="pdm-worker",
                keepalive=20,
            ) as client:
                mqtt_client_ref = client
                log.info("mqtt.connected", host=s.mqtt_host)
                interval = 2.0

                await client.subscribe(s.topic_features_sub, qos=1)
                await client.subscribe(s.topic_status_sub, qos=1)
                await client.subscribe(s.topic_burst_sub, qos=1)

                async for msg in client.messages:
                    try:
                        await route_message(msg)
                    except Exception as e:
                        log.exception("msg.handler_crashed", error=str(e))

        except aiomqtt.MqttError as e:
            mqtt_client_ref = None
            log.warning("mqtt.disconnected", error=str(e), retry_in_s=interval)
            await asyncio.sleep(interval)
            interval = min(interval * 2, 30.0)
        except asyncio.CancelledError:
            break


async def _healthz(request: web.Request) -> web.Response:
    ok = await db.health()
    body = {
        "status": "ok" if ok else "degraded",
        "db": "ok" if ok else "down",
        "mqtt": "ok" if mqtt_client_ref else "down",
        "model": anomaly_detector.version if anomaly_detector else "none",
        "counters": dict(counters),
    }
    return web.json_response(body, status=200 if ok else 503)


async def _metrics(request: web.Request) -> web.Response:
    lines = [
        "# HELP iotpdm_msgs_received Total messages received",
        "# TYPE iotpdm_msgs_received counter",
        *(f"iotpdm_{k} {v}" for k, v in counters.items()),
    ]
    return web.Response(text="\n".join(lines) + "\n", content_type="text/plain")


async def health_server() -> None:
    app = web.Application()
    app.router.add_get("/healthz", _healthz)
    app.router.add_get("/metrics", _metrics)
    runner = web.AppRunner(app)
    await runner.setup()
    site = web.TCPSite(runner, "0.0.0.0", 9100)
    await site.start()
    log.info("health.listening", port=9100)
    try:
        await shutdown_event.wait()
    finally:
        await runner.cleanup()


async def _load_model_poller() -> None:
    global anomaly_detector
    s = get_settings()
    path = Path(s.model_dir) / s.active_model_filename
    last_mtime = 0.0
    while not shutdown_event.is_set():
        try:
            mtime = path.stat().st_mtime if path.exists() else 0.0
            if mtime != last_mtime:
                anomaly_detector = AnomalyDetector.load(path)
                last_mtime = mtime
                log.info(
                    "model.loaded",
                    version=anomaly_detector.version,
                    path=str(path),
                )
        except Exception as e:
            log.warning("model.reload_failed", error=str(e))
        await asyncio.sleep(15)


async def _prime_zscore_windows() -> None:
    try:
        async with db.acquire() as conn:
            devices = await conn.fetch(
                "SELECT device_id FROM devices WHERE active = true"
            )
            for d in devices:
                vals = await recent_rms_values(
                    conn, d["device_id"], get_settings().worker_baseline_window
                )
                if vals:
                    zscore.prime(d["device_id"], vals)
                    log.info("zscore.primed", device_id=d["device_id"], n=len(vals))
    except Exception as e:
        log.warning("zscore.prime_failed", error=str(e))


async def run() -> None:
    loop = asyncio.get_running_loop()
    for sig in (signal.SIGINT, signal.SIGTERM):
        loop.add_signal_handler(sig, shutdown_event.set)

    await db.connect()
    await _prime_zscore_windows()

    global anomaly_detector
    s = get_settings()
    anomaly_detector = AnomalyDetector.load(
        Path(s.model_dir) / s.active_model_filename
    )

    try:
        await asyncio.gather(
            mqtt_loop(),
            health_server(),
            _load_model_poller(),
        )
    finally:
        await db.close()
        log.info("worker.shutdown.complete")
