# Multi-stage build for the ingest worker + FastAPI control plane.
# Base: python:3.12-slim-bookworm (multi-arch: works on Mac M-series, Pi 5 arm64, x86_64)

ARG PY_VERSION=3.12
FROM python:${PY_VERSION}-slim-bookworm AS base

ENV PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1 \
    PIP_NO_CACHE_DIR=1 \
    PIP_DISABLE_PIP_VERSION_CHECK=1 \
    UV_SYSTEM_PYTHON=1

# System deps: curl for healthcheck, build-essential + libpq-dev for any C ext fallback
RUN apt-get update && apt-get install -y --no-install-recommends \
        curl ca-certificates build-essential libpq-dev git \
    && rm -rf /var/lib/apt/lists/*

# uv: fast pip replacement
RUN pip install --no-cache-dir uv==0.5.7

WORKDIR /app

# Dependency layer (cached until pyproject.toml changes)
COPY pyproject.toml ./
RUN uv pip install --system \
      "aiomqtt==2.3.0" \
      "asyncpg==0.30.0" \
      "pydantic==2.9.2" \
      "pydantic-settings==2.6.1" \
      "structlog==24.4.0" \
      "tenacity==9.0.0" \
      "fastapi==0.115.5" \
      "uvicorn[standard]==0.32.1" \
      "scikit-learn==1.5.2" \
      "numpy==2.1.3" \
      "pandas==2.2.3" \
      "joblib==1.4.2" \
      "prometheus-fastapi-instrumentator==7.0.0" \
      "orjson==3.10.11" \
      "typer==0.13.1" \
      "httpx==0.27.2" \
      "sqlalchemy==2.0.36" \
      "psycopg[binary]==3.2.3" \
      "aiohttp==3.11.10"

# Source code (bind-mounted in dev; baked in prod)
COPY src/ ./src/

# Make our package importable
ENV PYTHONPATH=/app/src

# Non-root user
RUN useradd -m -u 1001 app && chown -R app:app /app
USER app

# Model directory
RUN mkdir -p /app/models

EXPOSE 9100 8080

# Default command: run the worker. The api container overrides this.
CMD ["python", "-m", "iot_pdm.ingest"]
