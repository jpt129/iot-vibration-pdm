"""FastAPI control-plane service."""
import subprocess

from fastapi import FastAPI, HTTPException
from prometheus_fastapi_instrumentator import Instrumentator
from pydantic import BaseModel

from iot_pdm.logging import configure_logging, get_logger
from iot_pdm.storage.db import db

configure_logging()
log = get_logger("api")

app = FastAPI(title="IoT PdM Control Plane", version="1.0.0")
Instrumentator().instrument(app).expose(app)


@app.on_event("startup")
async def _startup() -> None:
    await db.connect()


@app.on_event("shutdown")
async def _shutdown() -> None:
    await db.close()


@app.get("/healthz")
async def healthz() -> dict:
    ok = await db.health()
    return {"status": "ok" if ok else "degraded", "db": "ok" if ok else "down"}


@app.get("/devices")
async def devices() -> list[dict]:
    async with db.acquire() as conn:
        rows = await conn.fetch("SELECT * FROM devices ORDER BY device_id")
    return [dict(r) for r in rows]


@app.get("/models")
async def models() -> list[dict]:
    async with db.acquire() as conn:
        rows = await conn.fetch(
            "SELECT * FROM model_versions ORDER BY trained_at DESC LIMIT 20"
        )
    return [dict(r) for r in rows]


class TrainRequest(BaseModel):
    hours: int = 3
    contamination: str = "auto"
    activate: bool = True


@app.post("/retrain")
async def retrain(req: TrainRequest) -> dict:
    try:
        cmd = [
            "python", "-m", "iot_pdm.ml.train", "fit",
            "--hours", str(req.hours),
            "--contamination", req.contamination,
        ]
        if req.activate:
            cmd.append("--activate")
        out = subprocess.run(
            cmd, check=True, capture_output=True, text=True, timeout=180
        )
        return {
            "status": "ok",
            "stdout": out.stdout[-2000:],
            "stderr": out.stderr[-2000:],
        }
    except subprocess.CalledProcessError as e:
        raise HTTPException(
            status_code=500, detail={"stdout": e.stdout, "stderr": e.stderr}
        )
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))


@app.get("/stats")
async def stats() -> dict:
    async with db.acquire() as conn:
        total = await conn.fetchval("SELECT count(*) FROM features")
        dlq = await conn.fetchval("SELECT count(*) FROM dlq")
        anoms = await conn.fetchval("SELECT count(*) FROM features WHERE anomaly_flag")
        latest_row = await conn.fetchrow(
            "SELECT max(time) AS t, min(time) AS t0 FROM features"
        )
    return {
        "total_features": total,
        "dlq_rows": dlq,
        "anomalies": anoms,
        "latest_row_at": latest_row["t"],
        "first_row_at": latest_row["t0"],
    }
