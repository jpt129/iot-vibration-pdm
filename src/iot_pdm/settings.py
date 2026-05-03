"""Central configuration via pydantic-settings.

Reads from env vars (or .env file). Fails fast on first .get_settings() call
if any required setting is missing.
"""
from functools import lru_cache

from pydantic import Field
from pydantic_settings import BaseSettings, SettingsConfigDict


class Settings(BaseSettings):
    model_config = SettingsConfigDict(
        env_file=".env",
        env_file_encoding="utf-8",
        case_sensitive=False,
        extra="ignore",
    )

    # Postgres
    postgres_host: str = "timescale"
    postgres_port: int = 5432
    postgres_db: str = "iotpdm"
    postgres_user: str = "iotpdm"
    postgres_password: str = Field(..., min_length=8)

    # MQTT
    mqtt_host: str = "mosquitto"
    mqtt_port: int = 1883
    mqtt_user: str = "worker"
    mqtt_pass: str = Field(..., min_length=1)

    # Worker tuning
    worker_late_tolerance_s: int = 60
    worker_dlq_table: str = "dlq"
    worker_anomaly_threshold: float = 0.6
    worker_baseline_window: int = 300

    # Model
    model_dir: str = "/app/models"
    active_model_filename: str = "anomaly_active.joblib"

    # Logging
    log_level: str = "INFO"

    # Observability
    metrics_port: int = 9100

    @property
    def dsn(self) -> str:
        return (
            f"postgresql://{self.postgres_user}:{self.postgres_password}"
            f"@{self.postgres_host}:{self.postgres_port}/{self.postgres_db}"
        )

    @property
    def topic_features_sub(self) -> str:
        return "iotpdm/+/+/+/features"

    @property
    def topic_status_sub(self) -> str:
        return "iotpdm/+/+/+/status"

    @property
    def topic_burst_sub(self) -> str:
        return "iotpdm/+/+/+/burst"


@lru_cache
def get_settings() -> Settings:
    return Settings()
