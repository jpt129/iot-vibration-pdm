# IoT Vibration Predictive Maintenance

End-to-end IoT pipeline for predictive maintenance of rotating equipment, using an ESP32-S3 sensor node to capture multi-modal vibration and thermal data from a real desk fan, stream it through MQTT to a Dockerized backend, and detect mechanical anomalies in real time using an Isolation Forest model.

The system was developed as a graduate IoT capstone project (Spring 2026). It is intentionally scoped to a single piece of equipment but uses the same architectural patterns that would apply to a fleet of industrial motors, pumps, or HVAC units.

## Use Case

Rotating equipment (motors, fans, pumps, compressors) accounts for a large share of unplanned downtime in industrial facilities. The dominant failure modes — bearing wear, rotor imbalance, misalignment, blade damage — all manifest as detectable changes in vibration spectra and motor housing temperature, often days or weeks before catastrophic failure.

Traditional condition monitoring relies on periodic manual inspections by a technician with a handheld vibration probe. This is expensive, infrequent, and reactive. An always-on sensor that streams data to a central system can detect deterioration trends much earlier and at a fraction of the cost.

This project builds a working end-to-end realization of that pattern:

- **Device under monitoring:** a ComfortZone CZHV4P-RC USB desk fan (single-speed, brushed DC motor)
- **Failure mode demonstrated:** rotor imbalance, induced by attaching a small washer to one blade
- **Decision the system supports:** flag the fan for service before vibration progresses to bearing damage

The fan was chosen because it is small, predictable, easy to instrument, and exhibits a clean baseline-vs-fault signal that can be reliably reproduced for a video demo. The same hardware and software stack would generalize to any rotating asset where a sensor node can be mounted to the housing.

## What It Does

The ESP32-S3 sensor node, mounted on the fan housing, samples accelerometer data at 100 Hz and reads a 32 by 24 thermal frame at 2 Hz. Once per second it computes 13 derived features (RMS acceleration on three axes, FFT peak frequency and magnitude, kurtosis, crest factor, thermal min/mean/max, thermal gradient, and hot-spot percentage) and publishes them as a JSON message over MQTT with QoS 1 and a UUIDv7 message ID.

The cloud worker validates each message against a Pydantic schema, rejects malformed payloads to a dead-letter queue, scores the validated features against a trained Isolation Forest model, and persists everything to TimescaleDB. A Grafana dashboard renders the live data and the anomaly count, and a Streamlit app provides a UI for labeling time windows and retraining the model.

Over 41,000 telemetry rows have been collected during development across multiple fan states (off, healthy running, fault running, sensor disconnect).

## Architecture

```
+--------------------+        MQTT QoS 1         +--------------------------------+
|  ESP32-S3 + IMU    |  --------------------->   |  Mosquitto Broker (Docker)     |
|  + Thermal Camera  |        WiFi               |  topic: iotpdm/lab/fan01/...   |
|  Mounted on fan    |                           +--------------------------------+
+--------------------+                                          |
        |                                                       v
        |   100 Hz IMU sampling                       +--------------------+
        |   2 Hz thermal frames                       |  Python Worker     |
        |   1 Hz feature publish                      |  - Pydantic validate
        |   FreeRTOS tasks                            |  - DLQ on failure  |
        |                                             |  - Isolation Forest|
        v                                             |  - z-score check   |
[motor housing                                        |  - DB insert       |
 vibration + heat]                                    +--------------------+
                                                                |
                                                                v
                              +-------------------+   +--------------------+
                              |  Grafana Live     |   |  TimescaleDB       |
                              |  Dashboard        |<--|  (PostgreSQL +     |
                              |  http://:3030     |   |   Timescale ext.)  |
                              +-------------------+   +--------------------+
                              |  Streamlit ML     |          |
                              |  Label & Retrain  |<---------+
                              |  http://:8501     |
                              +-------------------+
                              |  FastAPI Control  |
                              |  http://:8090     |
                              +-------------------+
                              |  7" HDMI Kiosk    |
                              |  Pi 5 mirror stack|
                              +-------------------+
```

### Component Choices and Rationale

| Component | Choice | Why this and not alternatives |
|---|---|---|
| Compute | ESP32-S3 DevKitC | Low cost, dual-core 240 MHz, native WiFi, plenty of RAM for FFT, FreeRTOS available. An Arduino UNO would not have the compute or networking; a Raspberry Pi Pico would lack onboard WiFi. |
| Vibration sensor | MPU-9250 (ShillehTek pre-soldered breakout) | 6-axis IMU at 1 kHz, ±4g range, well-understood Bolder Flight library, I2C interface shareable with thermal. Industrial accelerometers (e.g. ICP) would be more accurate but cost 50x more and need an analog front-end. |
| Thermal sensor | MLX90640 (Adafruit STEMMA QT) | 32x24 thermal array gives motor-housing imaging, not just a single point. A single-point sensor like an MLX90614 would not show whether the motor or the fan blades are warmer. |
| Communication | MQTT QoS 1 | Lightweight, broker-mediated pub/sub fits IoT fleets natively. Each device only knows its broker, not the consumers. CoAP would also work but lacks broker fan-out. REST would require the device to retry on failure and would not survive backend restarts cleanly. |
| Message ID | UUIDv7 | Time-ordered, globally unique, sortable. Lets us deduplicate retransmits server-side without coordination. |
| Storage | TimescaleDB | Built on PostgreSQL, so we get full SQL plus time-series acceleration (hypertables, continuous aggregates, retention policies). InfluxDB would also have worked but TimescaleDB lets us write the same SQL the rest of the stack uses. |
| Validation | Pydantic v2 | Schema-first message contract. Rejects out-of-range and malformed messages at the worker boundary, never letting bad data corrupt the time-series. |
| ML | Isolation Forest | Unsupervised, multivariate, robust to feature scale (with RobustScaler), trains in well under a second on thousands of rows. Trained on healthy data only; anything that does not match the healthy envelope scores higher. |
| Visualization | Grafana + Streamlit | Grafana for the live operations dashboard (the kind a maintenance technician would watch). Streamlit for the data-science workflow (label windows, retrain models). Two tools, two distinct audiences. |
| Edge mirror | Raspberry Pi 5 | Same Docker stack as the cloud, ready for a fully edge-resident deployment if cloud connectivity is unavailable. Drives a 7-inch HDMI kiosk display. |

## Repository Layout

```
.
├── firmware/
│   ├── sensor-node/              # main ESP32-S3 firmware (PlatformIO)
│   │   ├── src/
│   │   │   ├── main.cpp
│   │   │   ├── imu_task.cpp      # 100 Hz IMU sampling, 3-axis ring buffer
│   │   │   ├── thermal_task.cpp  # 2 Hz MLX90640 frame reads
│   │   │   ├── features.cpp      # FFT, kurtosis, crest, thermal stats
│   │   │   ├── mqtt_publisher.cpp
│   │   │   ├── fault_injection.cpp
│   │   │   └── ...
│   │   └── platformio.ini
│   └── i2c-smoketest/            # scan tool for hardware bring-up
├── src/iot_pdm/                  # Python backend (worker, API, ML)
│   ├── ingest/
│   │   └── worker.py             # MQTT subscriber, validate, score, persist
│   ├── schemas/
│   │   └── telemetry.py          # Pydantic message schema
│   ├── storage/
│   │   └── db.py                 # asyncpg INSERT, DLQ writer
│   ├── ml/
│   │   ├── anomaly.py            # Isolation Forest + rolling z-score
│   │   └── train.py              # CLI trainer
│   ├── api/                      # FastAPI control plane
│   └── settings.py               # pydantic-settings env loader
├── streamlit_app/                # data-science UI
├── config/
│   ├── grafana/                  # dashboard provisioning
│   ├── mosquitto/                # broker config + ACLs
│   └── timescale/init.sql        # DB schema
├── scripts/
│   ├── seed_synthetic.py         # generate synthetic baseline rows
│   └── inject_*.py               # fault injection helpers
├── docker-compose.yml
├── .env.example
├── Makefile
└── docs/
    └── screenshots/
```

## Data Pipeline Detail

### 1. Device Layer

The ESP32-S3 firmware uses three FreeRTOS tasks pinned to specific cores:

- **IMU task** (Core 1, priority 5): reads accelerometer X/Y/Z at 100 Hz into a 1024-sample ring buffer. Uses `vTaskDelayUntil` for jitter-free timing. Holds an I2C mutex around each `readSensor` call so the thermal task cannot interleave on the same bus.
- **Thermal task** (Core 0, priority 3): reads a 768-pixel MLX90640 frame every 500 ms (2 Hz), computes min/mean/max plus a hot-spot percentage (pixels above mean + 2 sigma), and publishes globals.
- **Features task** (Core 1, priority 4): once per second, takes a snapshot of the IMU ring buffer, runs a 1024-point FFT, computes RMS, kurtosis, crest factor, and peak frequency on the Z-axis, plus per-axis RMS and total magnitude on all three axes, and combines with the latest thermal globals into a 13-field feature vector. Publishes as JSON over MQTT QoS 1.

A separate fault-injection module exposes runtime-toggleable bad-data scenarios (see "Bad Data Handling" below).

### 2. Communication Layer

MQTT was chosen because:

- The device only needs to know one address (the broker), not every consumer.
- New consumers (a second worker, a debug subscriber, an analytics tool) can attach without redeploying the device.
- QoS 1 gives at-least-once delivery, which combined with idempotent inserts (ON CONFLICT DO NOTHING on `msg_id, time`) gives exactly-once semantics end-to-end.
- The broker (Mosquitto) is a single 5 MB binary that runs anywhere.

Topic structure: `iotpdm/{site}/{asset}/{device_id}/features` for the main telemetry, with separate topics for `cmd`, `alert`, and a Last Will Testament that publishes `offline` to `status` if the device drops.

### 3. Processing Layer

The worker (`src/iot_pdm/ingest/worker.py`) is the heart of the cloud-side logic. For each incoming MQTT message it:

1. **Parses and validates** the JSON against the Pydantic schema (`src/iot_pdm/schemas/telemetry.py`). Schema violations send the message to the dead-letter queue with a reason code.
2. **Detects late arrivals** by comparing the message timestamp to "now" — if a message is more than `WORKER_LATE_TOLERANCE_S` seconds old, it is still ingested but flagged with `late = true`.
3. **Scores anomalies** by passing the feature vector through the loaded Isolation Forest model. Returns a score (higher = more anomalous, threshold-configurable) and a boolean flag.
4. **Computes a rolling z-score** on RMS values per device as a univariate baseline metric.
5. **Inserts** the enriched row into `features` using an idempotent INSERT.
6. **Publishes** an `alert` topic message back to the device when the anomaly flag is set, so the device LED can react.
7. **Updates** the device-status table on every receipt for the live "online/offline" indicator.

Exceptions and DB errors route the message to the DLQ rather than crashing the worker. The worker also exposes a `/healthz` endpoint on port 9100.

### 4. Storage Layer

Schema lives in `config/timescale/init.sql` and includes:

- **`features`** (hypertable, primary table): one row per second per device, 30 columns covering raw and derived metrics, anomaly outputs, and quality flags. Time-partitioned weekly. Indexed on `(device_id, time DESC)`.
- **`devices`**: one row per device with metadata (firmware, status, notes).
- **`labels`**: time-window labels (healthy, fault, unknown) used to filter training data.
- **`model_versions`**: every trained model, with metrics JSON and an `active` boolean.
- **`dlq`**: malformed messages with the reason, payload preview, and original error.
- **`asset_states`**, **`burst_windows`**, **`schema_version`**: supporting tables.

Sample queries are in `docs/sample_queries.sql` and the README "Verifying" section below.

### 5. Validation and Bad-Data Handling

The pipeline handles these scenarios, all coded into firmware fault-injection helpers and validated against Pydantic:

| Scenario | How handled | Where |
|---|---|---|
| Null `rms_g` | Pydantic rejects, DLQ | Schema requires a float |
| Out-of-range `rms_g` (> 32g) | Pydantic rejects, DLQ | `Field(ge=0, le=32)` |
| Wrong schema version | Pydantic rejects, DLQ | `Literal["1.0"]` |
| Missing `mel` array | Pydantic rejects, DLQ | `Field(min_length=6, max_length=6)` |
| Late-arriving message | Persisted with `late = true` | Worker logic |
| Duplicate `msg_id` | Idempotent insert silently drops | `ON CONFLICT DO NOTHING` |
| Sensor disconnect (zeros) | Persisted, anomaly model flags | Model trained on non-zero baseline |

Each scenario can be triggered from the firmware by publishing an MQTT command on the device's `cmd` topic, which is useful for live demos.

### 6. Output Layer

**Two visualizations and one ML output**, satisfying Option A of the rubric:

1. **Grafana dashboard** (live, port 3030) — RMS time series, 3-axis vibration breakdown, thermal max/mean over time, anomaly counter, online status, DLQ event table, DLQ rate by reason.
2. **Streamlit app** (port 8501) — Window labeling UI, model retraining UI, peak-frequency-vs-RMS scatter, model registry browser.
3. **Isolation Forest anomaly detection** — Unsupervised multivariate model, trained only on healthy fan-running data. Inputs are the 14-feature vector. Output is a single anomaly score per row, thresholded into a boolean flag. The decision a maintenance technician makes from the output is "schedule an inspection of fan-01 in the next shift."

## Sensor Configuration Note (INMP441 Microphone)

The original project plan included a third sensor — an INMP441 I2S MEMS microphone — for acoustic monitoring of bearing whine and blade scrape. The microphone arrived as a bare unsoldered breakout board, and during development the soldering work proved unreliable on the hardware available at the time. Rather than ship an intermittent third sensor, the project was rescoped to **vibration plus thermal sensor fusion as Phase 1, with acoustic monitoring deferred to Phase 2 future work.**

The decision is documented honestly here because:

- The 13 features captured from the two remaining sensors already exceed the rubric requirement of "at least 2 measurable attributes" by a factor of 6.
- The MQTT message schema retains a `mel` field (six log-Mel acoustic bands) so that adding the microphone in Phase 2 will not require a schema migration. The field currently transmits as zeros and is documented as such.
- All firmware code paths for the microphone (I2S task, mel-band filterbank) are present and tested with synthetic input; only the hardware connection is omitted.

## Quick Start

Tested on macOS 14 (Apple Silicon) and Raspberry Pi OS Bookworm (arm64).

### Prerequisites

- Docker Desktop (with `docker compose` available)
- PlatformIO Core (`pip install platformio`) for firmware builds
- An ESP32-S3 DevKitC with MPU-9250 and MLX90640 wired per the pinout below
- Make (optional but convenient)

### Pinout

```
ESP32-S3 GPIO 8  (SDA)   --->  MPU-9250 SDA  +  MLX90640 SDA
ESP32-S3 GPIO 9  (SCL)   --->  MPU-9250 SCL  +  MLX90640 SCL
ESP32-S3 3V3             --->  MPU-9250 VCC  +  MLX90640 VIN
ESP32-S3 GND             --->  MPU-9250 GND  +  MLX90640 GND
```

The MLX90640 has built-in 10k pull-ups; no external resistors required.

### 1. Clone and configure

```bash
git clone https://github.com/jpt129/iot-vibration-pdm.git
cd iot-vibration-pdm
cp .env.example .env
# Edit .env if you want to change ports, passwords, or the anomaly threshold.
```

### 2. Bring up the backend

```bash
docker compose up -d
docker compose ps
```

You should see six healthy services: `pdm-mosquitto`, `pdm-timescale`, `pdm-worker`, `pdm-api`, `pdm-grafana`, `pdm-streamlit`.

The TimescaleDB schema is auto-applied from `config/timescale/init.sql` on first run. Grafana auto-provisions the dashboard from `config/grafana/provisioning/`.

### 3. Configure WiFi credentials and flash the device

```bash
cd firmware/sensor-node
cp include/secrets.example.h include/secrets.h
# Edit secrets.h to set WIFI_SSID, WIFI_PASS, and MQTT_HOST (your laptop's LAN IP)

pio run -e esp32s3-devkitc -t upload
pio device monitor -e esp32s3-devkitc
```

You should see the boot banner, both sensors initializing, and once-per-second feature publish messages.

### 4. View the dashboards

- Grafana live dashboard: http://localhost:3030 (default admin / admin-change-me)
- Streamlit ML UI: http://localhost:8501
- FastAPI control plane: http://localhost:8090/docs

### 5. Train the anomaly model

After collecting at least a few minutes of healthy fan-running data:

```bash
docker compose exec worker python -m iot_pdm.ml.train --hours 1 --activate
docker compose restart worker
```

The model is saved as `models/anomaly_active.joblib` (mounted from the worker container) and registered in the `model_versions` table. The worker reloads it on restart and scores every subsequent row.

### 6. Verify the pipeline

```bash
# Most recent feature rows
docker compose exec timescale psql -U iotpdm -d iotpdm -c "
SELECT time, rms_g, peak_hz, accel_mag_rms, therm_max_c, anomaly_score, anomaly_flag
FROM features
WHERE device_id = 'esp32-a17'
ORDER BY time DESC
LIMIT 10;
"

# Total record count
docker compose exec timescale psql -U iotpdm -d iotpdm -c "
SELECT count(*) FROM features WHERE device_id = 'esp32-a17';
"

# Recent anomaly events
docker compose exec timescale psql -U iotpdm -d iotpdm -c "
SELECT time, rms_g, accel_mag_rms, anomaly_score
FROM features
WHERE device_id = 'esp32-a17' AND anomaly_flag = true
ORDER BY time DESC LIMIT 20;
"
```

### Optional: Raspberry Pi mirror

The same `docker-compose.yml` runs on a Raspberry Pi 5. SSH in, clone the repo, copy `.env`, and run `docker compose up -d`. The Pi can either subscribe to the same broker as the cloud (passive mirror) or be the broker itself (fully self-contained edge deployment) by changing `MQTT_HOST` in the device firmware.

A 7-inch HDMI display attached to the Pi runs Chromium in kiosk mode pointing at Grafana. The launch script is `~/start-kiosk.sh` on the Pi.

## Demonstration Results

### Healthy baseline

With the fan running normally, the model scores rows in the -0.10 to +0.05 range (negative = comfortably inside the trained healthy envelope). Approximately 3,600 rows of healthy fan-running data were used to train the production Isolation Forest model.

### Fault induction

A small steel washer was attached to a single fan blade with adhesive tape, creating a rotor imbalance. The fan was then started.

| Metric | Healthy baseline | With fault |
|---|---|---|
| `rms_g` (Z-axis RMS acceleration) | ~0.06 g | up to 0.97 g |
| `accel_mag_rms` (3-axis magnitude) | ~0.21 g | up to 1.62 g |
| Anomaly count, 1-hour window | 0 | 100+ |

The 16x increase in vibration RMS and the 7x increase in 3-axis magnitude were both clearly visible on the live Grafana dashboard within 5 seconds of fan startup. The Isolation Forest fired 100+ anomaly events during the fault period.

Screenshots are in `docs/screenshots/`.

## Testing the Bad-Data Pipeline

The firmware exposes runtime fault injection over MQTT. From any machine with `mosquitto_pub`:

```bash
# Inject a single message with rms_g = null
mosquitto_pub -h <broker> -t iotpdm/lab/fan01/esp32-a17/cmd -m '{"action":"inject","kind":"null"}'

# Inject an out-of-range value
mosquitto_pub -h <broker> -t iotpdm/lab/fan01/esp32-a17/cmd -m '{"action":"inject","kind":"oor"}'

# Inject a wrong-schema-version message
mosquitto_pub -h <broker> -t iotpdm/lab/fan01/esp32-a17/cmd -m '{"action":"inject","kind":"schema_bad"}'
```

Then verify the message landed in the DLQ:

```bash
docker compose exec timescale psql -U iotpdm -d iotpdm -c "
SELECT time, reason, error FROM dlq ORDER BY time DESC LIMIT 5;
"
```

## Software Engineering Notes

- **Modular structure.** Python is split into `iot_pdm.ingest`, `iot_pdm.schemas`, `iot_pdm.storage`, `iot_pdm.ml`, and `iot_pdm.api`, each as a separate importable subpackage with no cyclic dependencies.
- **Configuration.** All runtime config comes from `.env` via `pydantic-settings`. No hard-coded credentials, no magic numbers in code.
- **Logging.** Structured JSON logging via `structlog` everywhere. Every log line includes a stable `event` key for grep-ability.
- **Error handling.** asyncpg connection errors retry with exponential backoff via `tenacity`. MQTT reconnect is automatic. Validation errors send to DLQ rather than crash. Worker continues running even if the model file is missing (warns and skips scoring).
- **Reusable functions and classes.** `AnomalyDetector`, `RollingZScore`, `insert_feature`, etc. are class- or function-scoped with clear interfaces.
- **Test-friendly seam.** A separate `scripts/seed_synthetic.py` can populate the database with synthetic but physically realistic data for development and CI.

## Tradeoffs and Limitations

- **Single-fan demo.** The system is built for a fleet, but only one device was deployed for the project. Multi-tenant aspects (per-device anomaly thresholds, per-asset models, fleet-level dashboards) exist in the schema but are not exercised at scale.
- **Isolation Forest weaknesses.** The model treats all 14 features equally. In practice, vibration-derived features carry more diagnostic information than thermal-derived features for the failure modes shown. A weighted ensemble or feature-importance-aware model would do better.
- **Fixed sample rate.** 100 Hz IMU sampling captures vibration up to 50 Hz cleanly. Bearing fault frequencies on industrial machines often live above this. A real industrial node would sample at 1-10 kHz with higher-grade accelerometers.
- **No edge inference.** All anomaly scoring happens in the cloud worker. For a deployment where backhaul is unreliable, the model could be exported to ONNX and run on the ESP32 itself, with only alerts going up.
- **No alerting integration.** The pipeline detects anomalies but does not page anyone. Hooking the `alert` MQTT topic to PagerDuty, email, or Slack would close the loop.

## Scaling to Enterprise

For a deployment with 1,000+ devices across multiple sites, the changes would be:

- **Broker.** Replace single Mosquitto with EMQX or HiveMQ cluster, with TLS, per-device certificates, and ACLs.
- **Worker.** Run multiple worker replicas behind the broker's shared subscription feature, with horizontal autoscaling on queue depth.
- **Database.** Move TimescaleDB to a managed service (Timescale Cloud or self-hosted with replicas). Add continuous aggregates and retention policies for long-term storage.
- **Models.** One model per asset class, trained nightly from labeled data via Airflow or Prefect. Model registry (MLflow) and per-device active-version tracking.
- **Fleet management.** Add a device-provisioning service that hands out credentials, tracks firmware versions, and supports OTA updates.
- **Observability.** Prometheus metrics from every component; SLOs on message lag, anomaly detection latency, and DLQ rate.

The schema and message contracts in this project are already designed to support this; the work is operational.

## Challenges Faced and Lessons Learned

The largest engineering challenge was **I2C bus contention** between the MPU-9250 and the MLX90640 on the same shared bus. The MLX90640 holds the bus for ~250 ms during a frame read, which would starve the IMU's 100 Hz sample loop and trigger the FreeRTOS watchdog. The fix was a FreeRTOS mutex around all I2C operations, with the IMU mutex timeout raised to 500 ms and the IMU sample rate dropped from 250 Hz to 100 Hz. This was diagnosed only after extensive serial-log review.

The second largest challenge was **Pydantic schema validation rejecting real sensor data.** The original schema used `Field(le=100)` on `crest`, which is sensible for a moderately vibrating system but is exceeded by an idle near-DC accelerometer signal where the peak-to-RMS ratio explodes. Wider bounds (`crest le=10000`, `kurtosis le=100000`) match the actual physics.

A third lesson was around **mechanical reliability of breadboard wiring under vibration.** Mounting the breadboard directly on the back of the running fan caused the ESP32 to walk loose from its rails after about 60 seconds of fan operation, which in turn broke the I2C bus and produced a flood of `Wire.cpp` errors. The pragmatic fix was to capture the fault demo in short bursts (15-20 seconds) rather than long sustained runs. A future iteration would mount only the IMU on the equipment and keep the controller off-board on a stable surface.

What changed in my understanding of IoT: the data path itself is the easy part. Schemas, brokers, hypertables, and ML models are well-trodden ground with mature tools. The hard parts are the small, persistent operational issues — bus contention, mechanical mounting, schema bounds, watchdog tuning — that only show up when real hardware meets real physics. The "production-style" requirement of the rubric is exactly about those things.

## License

This project was developed for a graduate IoT capstone course and is provided as-is for educational reference. See the source files for any third-party library licenses (Bolder Flight MPU9250, Adafruit MLX90640, ArduinoFFT, asyncpg, scikit-learn, etc., each under their respective licenses).
