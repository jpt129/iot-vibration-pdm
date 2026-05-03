"""CLI trainer.

Usage:
    python -m iot_pdm.ml.train fit --hours 3 --activate
"""
from __future__ import annotations

import asyncio
import json
import shutil
from datetime import datetime, timezone
from pathlib import Path

import asyncpg
import pandas as pd
import typer

from iot_pdm.logging import configure_logging, get_logger
from iot_pdm.ml.anomaly import save, train
from iot_pdm.settings import get_settings

app = typer.Typer(no_args_is_help=True)


async def _load_healthy_df(hours: int, device_id: str | None) -> pd.DataFrame:
    s = get_settings()
    conn = await asyncpg.connect(s.dsn)
    try:
        q = """
            SELECT time, device_id,
                   rms_g, kurtosis, crest, peak_hz, peak_mag,
                   mel_0, mel_1, mel_2, mel_3, mel_4, mel_5,
                   therm_min_c, therm_mean_c, therm_max_c,
                   quality
            FROM features
            WHERE time > now() - ($1 || ' hours')::interval
              AND quality >= 80
              AND late = false
              AND NOT EXISTS (
                SELECT 1 FROM labels l
                WHERE l.device_id = features.device_id
                  AND features.time BETWEEN l.window_start AND l.window_end
                  AND l.state <> 'healthy'
              )
        """
        args: list = [str(hours)]
        if device_id:
            q += " AND device_id = $2"
            args.append(device_id)
        rows = await conn.fetch(q, *args)
    finally:
        await conn.close()
    return pd.DataFrame([dict(r) for r in rows])


async def _register_model(version: str, path: str, metrics: dict, activate: bool) -> None:
    s = get_settings()
    conn = await asyncpg.connect(s.dsn)
    try:
        if activate:
            await conn.execute(
                "UPDATE model_versions SET active = false "
                "WHERE algorithm = 'isolation_forest'"
            )
        await conn.execute(
            "INSERT INTO model_versions "
            "(version, algorithm, path, trained_on_rows, metrics_json, active) "
            "VALUES ($1, 'isolation_forest', $2, $3, $4::jsonb, $5)",
            version, path, metrics.get("rows_trained"), json.dumps(metrics), activate,
        )
    finally:
        await conn.close()


@app.command()
def fit(
    hours: int = typer.Option(3, help="Window of recent data to train on"),
    contamination: str = typer.Option("auto", help="'auto' or float 0-0.5"),
    device_id: str | None = typer.Option(None),
    activate: bool = typer.Option(True, help="Make this the live model"),
):
    configure_logging()
    log = get_logger("train")
    s = get_settings()
    df = asyncio.run(_load_healthy_df(hours, device_id))
    log.info("dataset.loaded", rows=len(df), hours=hours)
    if len(df) < 500:
        log.error("dataset.too_small", rows=len(df))
        raise typer.Exit(code=2)

    cval: str | float = contamination if contamination == "auto" else float(contamination)
    pipeline, metrics = train(df, contamination=cval)

    version = datetime.now(timezone.utc).strftime("v%Y%m%d_%H%M%S")
    out_path = Path(s.model_dir) / f"anomaly_{version}.joblib"
    save(pipeline, metrics, out_path, version)

    if activate:
        active = Path(s.model_dir) / s.active_model_filename
        shutil.copy2(out_path, active)
        log.info("model.activated", path=str(active))

    asyncio.run(_register_model(version, str(out_path), metrics, activate))
    log.info("train.done", **metrics)


if __name__ == "__main__":
    app()
