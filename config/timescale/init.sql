-- ================================================================
-- IoT Vibration PdM - schema initialization
-- Runs ONCE at first container boot (Postgres entrypoint pattern).
-- ================================================================

CREATE EXTENSION IF NOT EXISTS timescaledb;
CREATE EXTENSION IF NOT EXISTS "uuid-ossp";
CREATE EXTENSION IF NOT EXISTS pg_stat_statements;

-- ----------------------------------------------------------------
-- Device registry (static metadata; joined into the features stream)
-- ----------------------------------------------------------------
CREATE TABLE IF NOT EXISTS devices (
  device_id    TEXT PRIMARY KEY,
  site         TEXT NOT NULL,
  asset        TEXT NOT NULL,
  asset_type   TEXT NOT NULL,
  installed_at TIMESTAMPTZ NOT NULL DEFAULT now(),
  location     TEXT,
  notes        TEXT,
  firmware     TEXT,
  active       BOOLEAN NOT NULL DEFAULT true
);

-- Seed our one node so the FK joins work from day 1
INSERT INTO devices (device_id, site, asset, asset_type, location, firmware)
VALUES ('esp32-a17', 'lab', 'fan01', 'fan', 'demo-bench', '1.0.0')
ON CONFLICT (device_id) DO NOTHING;

-- ----------------------------------------------------------------
-- Asset state history - what condition was the fan IN when we sampled?
-- ----------------------------------------------------------------
CREATE TABLE IF NOT EXISTS asset_states (
  id         BIGSERIAL PRIMARY KEY,
  device_id  TEXT NOT NULL REFERENCES devices(device_id),
  state      TEXT NOT NULL,
  started_at TIMESTAMPTZ NOT NULL,
  ended_at   TIMESTAMPTZ,
  notes      TEXT
);
CREATE INDEX IF NOT EXISTS idx_asset_states_device_time
  ON asset_states(device_id, started_at DESC);

-- ----------------------------------------------------------------
-- Main feature hypertable - 1 Hz rows, every feature vector
-- ----------------------------------------------------------------
CREATE TABLE IF NOT EXISTS features (
  time          TIMESTAMPTZ       NOT NULL,
  device_id     TEXT              NOT NULL,
  site          TEXT              NOT NULL,
  asset         TEXT              NOT NULL,
  msg_id        UUID              NOT NULL,
  seq           INTEGER,
  quality       SMALLINT,

  rms_g         DOUBLE PRECISION,
  kurtosis      DOUBLE PRECISION,
  crest         DOUBLE PRECISION,

  peak_hz       DOUBLE PRECISION,
  peak_mag      DOUBLE PRECISION,

  mel_0         DOUBLE PRECISION,
  mel_1         DOUBLE PRECISION,
  mel_2         DOUBLE PRECISION,
  mel_3         DOUBLE PRECISION,
  mel_4         DOUBLE PRECISION,
  mel_5         DOUBLE PRECISION,

  therm_min_c   DOUBLE PRECISION,
  therm_mean_c  DOUBLE PRECISION,
  therm_max_c   DOUBLE PRECISION,

  anomaly_score DOUBLE PRECISION,
  anomaly_flag  BOOLEAN,
  z_rms         DOUBLE PRECISION,
  late          BOOLEAN NOT NULL DEFAULT false,

  ingested_at   TIMESTAMPTZ       NOT NULL DEFAULT now(),

  PRIMARY KEY (msg_id, time)
);

SELECT create_hypertable('features', 'time',
  chunk_time_interval => INTERVAL '6 hours',
  if_not_exists       => true);

CREATE INDEX IF NOT EXISTS idx_features_device_time
  ON features (device_id, time DESC);

CREATE INDEX IF NOT EXISTS idx_features_asset_anomaly
  ON features (asset, time DESC)
  WHERE anomaly_flag = true;

CREATE INDEX IF NOT EXISTS idx_features_msg_id
  ON features (msg_id);

-- ----------------------------------------------------------------
-- Raw burst windows - 1/min 1024-sample Z-axis dumps
-- ----------------------------------------------------------------
CREATE TABLE IF NOT EXISTS burst_windows (
  time        TIMESTAMPTZ NOT NULL,
  device_id   TEXT NOT NULL,
  axis        CHAR(1) NOT NULL DEFAULT 'Z',
  sample_rate_hz INTEGER NOT NULL,
  samples_g   REAL[] NOT NULL,
  msg_id      UUID NOT NULL,
  PRIMARY KEY (msg_id, time)
);
SELECT create_hypertable('burst_windows', 'time',
  chunk_time_interval => INTERVAL '1 day',
  if_not_exists       => true);

-- ----------------------------------------------------------------
-- Dead-letter queue - bad messages routed here for forensics
-- ----------------------------------------------------------------
CREATE TABLE IF NOT EXISTS dlq (
  id         BIGSERIAL PRIMARY KEY,
  time       TIMESTAMPTZ NOT NULL DEFAULT now(),
  topic      TEXT,
  reason     TEXT NOT NULL,
  device_id  TEXT,
  payload    TEXT,
  error      TEXT
);
CREATE INDEX IF NOT EXISTS idx_dlq_time ON dlq (time DESC);
CREATE INDEX IF NOT EXISTS idx_dlq_reason ON dlq (reason, time DESC);

-- ----------------------------------------------------------------
-- Labels - populated by Streamlit app for supervised training
-- ----------------------------------------------------------------
CREATE TABLE IF NOT EXISTS labels (
  id           BIGSERIAL PRIMARY KEY,
  device_id    TEXT NOT NULL,
  window_start TIMESTAMPTZ NOT NULL,
  window_end   TIMESTAMPTZ NOT NULL,
  state        TEXT NOT NULL,
  confidence   REAL NOT NULL DEFAULT 1.0,
  labeled_by   TEXT NOT NULL DEFAULT 'streamlit',
  labeled_at   TIMESTAMPTZ NOT NULL DEFAULT now(),
  notes        TEXT
);
CREATE INDEX IF NOT EXISTS idx_labels_device_window
  ON labels (device_id, window_start, window_end);

-- ----------------------------------------------------------------
-- Model registry - joblib blobs on disk; this tracks metadata
-- ----------------------------------------------------------------
CREATE TABLE IF NOT EXISTS model_versions (
  id            BIGSERIAL PRIMARY KEY,
  version       TEXT NOT NULL,
  algorithm     TEXT NOT NULL,
  path          TEXT NOT NULL,
  trained_at    TIMESTAMPTZ NOT NULL DEFAULT now(),
  trained_on_rows INTEGER,
  metrics_json  JSONB,
  active        BOOLEAN NOT NULL DEFAULT false,
  notes         TEXT
);
CREATE UNIQUE INDEX IF NOT EXISTS idx_model_active
  ON model_versions (algorithm) WHERE active = true;

-- ================================================================
-- CONTINUOUS AGGREGATES - pre-computed roll-ups for dashboard speed
-- ================================================================
CREATE MATERIALIZED VIEW IF NOT EXISTS features_per_minute
WITH (timescaledb.continuous) AS
SELECT
  time_bucket('1 minute', time)                    AS bucket,
  device_id,
  asset,
  avg(rms_g)                                        AS avg_rms_g,
  max(rms_g)                                        AS max_rms_g,
  avg(peak_hz)                                      AS avg_peak_hz,
  avg(therm_max_c)                                  AS avg_therm_max,
  avg(anomaly_score)                                AS avg_anomaly_score,
  sum(CASE WHEN anomaly_flag THEN 1 ELSE 0 END)     AS n_anomalies,
  count(*)                                          AS n_rows
FROM features
GROUP BY bucket, device_id, asset;

SELECT add_continuous_aggregate_policy('features_per_minute',
  start_offset    => INTERVAL '1 hour',
  end_offset      => INTERVAL '30 seconds',
  schedule_interval => INTERVAL '30 seconds',
  if_not_exists   => true);

-- ================================================================
-- COMPRESSION - keep storage bounded
-- ================================================================
ALTER TABLE features SET (
  timescaledb.compress,
  timescaledb.compress_segmentby = 'device_id',
  timescaledb.compress_orderby   = 'time DESC'
);
SELECT add_compression_policy('features', INTERVAL '2 days',
  if_not_exists => true);

-- ================================================================
-- Schema version marker
-- ================================================================
CREATE TABLE IF NOT EXISTS schema_version (
  version     TEXT PRIMARY KEY,
  applied_at  TIMESTAMPTZ NOT NULL DEFAULT now()
);
INSERT INTO schema_version (version) VALUES ('1.0.0') ON CONFLICT DO NOTHING;
