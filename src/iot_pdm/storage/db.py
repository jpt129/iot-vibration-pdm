"""asyncpg pool + per-query helpers. All DB I/O goes through here."""
from __future__ import annotations

from contextlib import asynccontextmanager

import asyncpg
from tenacity import (
    retry,
    retry_if_exception_type,
    stop_after_attempt,
    wait_exponential,
)

from iot_pdm.logging import get_logger
from iot_pdm.settings import get_settings

log = get_logger()


class DB:
    def __init__(self) -> None:
        self._pool: asyncpg.Pool | None = None

    async def connect(self) -> None:
        s = get_settings()
        self._pool = await asyncpg.create_pool(
            dsn=s.dsn,
            min_size=2,
            max_size=10,
            command_timeout=10,
            timeout=10,
        )
        log.info("db.connected", dsn_host=s.postgres_host, db=s.postgres_db)

    async def close(self) -> None:
        if self._pool:
            await self._pool.close()
            log.info("db.closed")

    @asynccontextmanager
    async def acquire(self):
        assert self._pool, "db.connect() not called"
        async with self._pool.acquire() as conn:
            yield conn

    async def health(self) -> bool:
        try:
            async with self.acquire() as c:
                val = await c.fetchval("SELECT 1")
                return val == 1
        except Exception as e:
            log.warning("db.health_failed", error=str(e))
            return False


# Singleton
db = DB()


@retry(
    stop=stop_after_attempt(6),
    wait=wait_exponential(multiplier=0.5, max=30),
    retry=retry_if_exception_type(
        (asyncpg.PostgresConnectionError, ConnectionError, TimeoutError)
    ),
    reraise=True,
)
async def insert_feature(conn, row: dict) -> None:
    """Insert a feature row; ON CONFLICT DO NOTHING makes it idempotent."""
    await conn.execute(
        """
        INSERT INTO features (
            time, device_id, site, asset, msg_id, seq, quality,
            rms_g, kurtosis, crest, peak_hz, peak_mag,
            mel_0, mel_1, mel_2, mel_3, mel_4, mel_5,
            therm_min_c, therm_mean_c, therm_max_c,
            accel_x_rms, accel_y_rms, accel_mag_rms,
            therm_gradient_c, therm_hotspot_pct,
            anomaly_score, anomaly_flag, z_rms, late
        ) VALUES (
            $1,$2,$3,$4,$5,$6,$7,
            $8,$9,$10,$11,$12,
            $13,$14,$15,$16,$17,$18,
            $19,$20,$21,
            $22,$23,$24,
            $25,$26,
            $27,$28,$29,$30
        )
        ON CONFLICT (msg_id, time) DO NOTHING
        """,
        row["time"], row["device_id"], row["site"], row["asset"], row["msg_id"],
        row["seq"], row["quality"],
        row["rms_g"], row["kurtosis"], row["crest"], row["peak_hz"], row["peak_mag"],
        *row["mel"],
        row["therm_min_c"], row["therm_mean_c"], row["therm_max_c"],
        row.get("accel_x_rms"), row.get("accel_y_rms"), row.get("accel_mag_rms"),
        row.get("therm_gradient_c"), row.get("therm_hotspot_pct"),
        row.get("anomaly_score"), row.get("anomaly_flag"),
        row.get("z_rms"), row.get("late", False),
    )


async def insert_dlq(
    conn,
    *,
    topic: str,
    reason: str,
    payload: str,
    device_id: str | None = None,
    error: str | None = None,
) -> None:
    await conn.execute(
        "INSERT INTO dlq (topic, reason, payload, device_id, error) "
        "VALUES ($1, $2, $3, $4, $5)",
        topic, reason, payload[:8000], device_id, (error or "")[:2000],
    )


async def upsert_device_status(
    conn, *, device_id: str, status: str, fw: str | None
) -> None:
    await conn.execute(
        "UPDATE devices SET firmware = COALESCE($2, firmware), notes = $3 "
        "WHERE device_id = $1",
        device_id, fw, f"status={status}",
    )


async def recent_rms_values(
    conn, device_id: str, window_seconds: int
) -> list[float]:
    rows = await conn.fetch(
        "SELECT rms_g FROM features WHERE device_id = $1 "
        "AND time > now() - ($2 || ' seconds')::interval "
        "AND late = false ORDER BY time ASC",
        device_id, str(window_seconds),
    )
    return [r["rms_g"] for r in rows if r["rms_g"] is not None]
