"""Isolation Forest + rolling z-score anomaly detector.

- Isolation Forest: unsupervised, multivariate, robust.
- Rolling z-score: simple univariate baseline for comparison.
"""
from __future__ import annotations

import os
import time
from pathlib import Path

import joblib
import numpy as np
import pandas as pd
from sklearn.ensemble import IsolationForest
from sklearn.pipeline import Pipeline
from sklearn.preprocessing import RobustScaler

from iot_pdm.logging import get_logger
from iot_pdm.settings import get_settings

log = get_logger()

FEATURE_COLS = [
    "rms_g", "kurtosis", "crest", "peak_hz", "peak_mag",
    "mel_0", "mel_1", "mel_2", "mel_3", "mel_4", "mel_5",
    "therm_min_c", "therm_mean_c", "therm_max_c",
]


class AnomalyDetector:
    """Wraps a joblib-persisted sklearn Pipeline.

    Decision function: higher = MORE anomalous (we flip sklearn's convention).
    """

    def __init__(self, model: Pipeline | None = None, version: str = "none") -> None:
        self.model = model
        self.version = version
        self.threshold = get_settings().worker_anomaly_threshold

    @classmethod
    def load(cls, path: str | Path) -> "AnomalyDetector":
        if not os.path.exists(path):
            log.warning("model.missing", path=str(path))
            return cls(model=None, version="none")
        bundle = joblib.load(path)
        return cls(model=bundle["pipeline"], version=bundle.get("version", "unknown"))

    def is_ready(self) -> bool:
        return self.model is not None

    def score(self, features: dict) -> tuple[float | None, bool | None]:
        """Returns (anomaly_score, anomaly_flag)."""
        if self.model is None:
            return None, None
        x = np.array([[features[c] for c in FEATURE_COLS]], dtype=float)
        raw = float(self.model.decision_function(x)[0])
        score = -raw  # higher = more anomalous
        return score, score >= self.threshold


def train(
    df: pd.DataFrame,
    contamination: str | float = "auto",
    n_estimators: int = 200,
    random_state: int = 42,
) -> tuple[Pipeline, dict]:
    """Train an Isolation Forest on healthy feature data."""
    X = df[FEATURE_COLS].to_numpy(dtype=float)

    pipeline = Pipeline([
        ("scaler", RobustScaler()),
        ("iforest", IsolationForest(
            n_estimators=n_estimators,
            contamination=contamination,
            max_samples="auto",
            bootstrap=False,
            n_jobs=-1,
            random_state=random_state,
        )),
    ])
    t0 = time.time()
    pipeline.fit(X)
    fit_s = time.time() - t0

    scores = -pipeline.decision_function(X)
    metrics = {
        "rows_trained": int(len(df)),
        "fit_seconds": round(fit_s, 3),
        "score_mean": float(scores.mean()),
        "score_p95": float(np.percentile(scores, 95)),
        "score_p99": float(np.percentile(scores, 99)),
        "n_estimators": n_estimators,
        "contamination": str(contamination),
    }
    log.info("model.trained", **metrics)
    return pipeline, metrics


def save(pipeline: Pipeline, metrics: dict, path: str | Path, version: str) -> None:
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    joblib.dump({"pipeline": pipeline, "metrics": metrics, "version": version}, path)
    log.info("model.saved", path=str(path), version=version)


class RollingZScore:
    """Maintains a rolling window of RMS values per device.

    In-process state - on worker restart, prime() reloads from DB.
    """

    def __init__(self, window_seconds: int = 300) -> None:
        self.window_seconds = window_seconds
        self._windows: dict[str, list[float]] = {}

    def prime(self, device_id: str, values: list[float]) -> None:
        self._windows[device_id] = list(values[-self.window_seconds:])

    def update(self, device_id: str, rms_g: float) -> float | None:
        w = self._windows.setdefault(device_id, [])
        w.append(rms_g)
        if len(w) > self.window_seconds:
            del w[: len(w) - self.window_seconds]
        if len(w) < 30:
            return None
        mu = float(np.mean(w))
        sd = float(np.std(w)) or 1e-6
        return (rms_g - mu) / sd
