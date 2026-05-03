# Streamlit labeling + retrain app
FROM python:3.12-slim-bookworm

ENV PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1 \
    PIP_NO_CACHE_DIR=1

RUN apt-get update && apt-get install -y --no-install-recommends \
        curl ca-certificates build-essential libpq-dev \
    && rm -rf /var/lib/apt/lists/*

RUN pip install --no-cache-dir uv==0.5.7
WORKDIR /app

RUN uv pip install --system \
      "streamlit==1.40.1" \
      "asyncpg==0.30.0" \
      "psycopg[binary]==3.2.3" \
      "pandas==2.2.3" \
      "numpy==2.1.3" \
      "scikit-learn==1.5.2" \
      "plotly==5.24.1" \
      "joblib==1.4.2" \
      "pydantic==2.9.2" \
      "pydantic-settings==2.6.1" \
      "httpx==0.27.2"

COPY src/ ./src/
COPY streamlit_app/ ./streamlit_app/

ENV PYTHONPATH=/app/src:/app

RUN useradd -m -u 1001 app && chown -R app:app /app
USER app

EXPOSE 8501
CMD ["streamlit", "run", "streamlit_app/app.py", \
     "--server.port=8501", "--server.address=0.0.0.0", \
     "--server.headless=true", "--browser.gatherUsageStats=false"]
