"""Pydantic v2 schemas for incoming MQTT messages.

Every message passes through these. ValidationError -> DLQ.
"""
from datetime import datetime, timedelta, timezone
from typing import Annotated, Literal
from uuid import UUID

from pydantic import (
    AwareDatetime,
    BaseModel,
    ConfigDict,
    Field,
    StringConstraints,
    field_validator,
)


class Metrics(BaseModel):
    model_config = ConfigDict(extra="forbid")

    # Time-domain
    rms_g: float = Field(ge=0.0, le=32.0)
    kurtosis: float = Field(ge=0.0, le=100000.0)
    crest: float = Field(ge=0.0, le=10000.0)
    # Frequency-domain
    peak_hz: float = Field(ge=0.0, le=500.0)
    peak_mag: float = Field(ge=0.0)
    # Acoustic - 6 log-Mel bands
    mel: list[float] = Field(min_length=6, max_length=6)
    # Thermal
    therm_min_c: float = Field(ge=-40.0, le=150.0)
    therm_mean_c: float = Field(ge=-40.0, le=150.0)
    therm_max_c: float = Field(ge=-40.0, le=150.0)

    @field_validator("mel")
    @classmethod
    def mel_finite(cls, v: list[float]) -> list[float]:
        for x in v:
            if not (-1e6 < x < 1e6):
                raise ValueError(f"mel value out of sane range: {x}")
        return v


DeviceID = Annotated[str, StringConstraints(pattern=r"^[a-zA-Z0-9_\-]{1,64}$")]
Site = Annotated[str, StringConstraints(pattern=r"^[a-zA-Z0-9_\-]{1,32}$")]
Asset = Annotated[str, StringConstraints(pattern=r"^[a-zA-Z0-9_\-]{1,32}$")]


class TelemetryMessage(BaseModel):
    """A 1-Hz feature vector from the ESP32."""

    model_config = ConfigDict(extra="forbid")

    schema_version: Literal["1.0"]
    msg_id: UUID
    device_id: DeviceID
    site: Site
    asset: Asset
    fw: str = Field(min_length=1, max_length=32)
    seq: int = Field(ge=0, le=2_147_483_647)
    quality: int = Field(ge=0, le=100)
    timestamp: AwareDatetime
    metrics: Metrics

    @field_validator("timestamp")
    @classmethod
    def not_too_far_future(cls, v: datetime) -> datetime:
        now = datetime.now(timezone.utc)
        if v > now + timedelta(minutes=2):
            raise ValueError(f"timestamp too far in future: {v.isoformat()}")
        if v < now - timedelta(days=30):
            raise ValueError(f"timestamp implausibly old: {v.isoformat()}")
        return v


class StatusMessage(BaseModel):
    model_config = ConfigDict(extra="allow")

    status: Literal["online", "offline"]
    fw: str | None = None
    reason: str | None = None
    uptime_s: int | None = None


class BurstMessage(BaseModel):
    """A 1024-sample raw Z-axis burst (~1/min)."""

    model_config = ConfigDict(extra="forbid")

    schema_version: Literal["1.0"]
    msg_id: UUID
    device_id: DeviceID
    timestamp: AwareDatetime
    axis: Literal["X", "Y", "Z"] = "Z"
    sample_rate_hz: int = Field(ge=1, le=10000)
    samples_g: list[float] = Field(min_length=64, max_length=4096)
