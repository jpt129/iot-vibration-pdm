// mqtt_publisher.cpp - WiFi + MQTT client task.
//
// - Connects to WiFi on boot
// - Establishes MQTT session with QoS 1, LWT (offline status)
// - Drains feature queue, serializes JSON, publishes on per-second cadence
// - Periodic status heartbeat (online + uptime)
// - Subscribes to cmd topic for fault-injection / reboot

#include "mqtt_publisher.h"
#include "config.h"
#include "uuid.h"
#include "fault_injection.h"
#include "led.h"
#include "secrets.h"

#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <time.h>

static WiFiClient   wifi_client;
static PubSubClient mqtt(wifi_client);

// Feature queue - small ring buffer (we drop oldest if full)
#define FEATURE_QUEUE_SIZE 8
static FeatureVector feature_queue[FEATURE_QUEUE_SIZE];
static volatile uint8_t q_head = 0;  // write
static volatile uint8_t q_tail = 0;  // read
static portMUX_TYPE   q_mux = portMUX_INITIALIZER_UNLOCKED;

// Topic strings - built once at boot
static char topic_features[160];
static char topic_status[160];
static char topic_alert[160];
static char topic_cmd[160];
static char topic_dlq[160];

static volatile bool connected_flag = false;

// ---------- Internal helpers ----------

static void build_topics() {
  snprintf(topic_features, sizeof(topic_features),
           "%s/%s/%s/%s/%s",
           TOPIC_PREFIX, DEVICE_SITE, DEVICE_ASSET, DEVICE_ID, TOPIC_FEATURES);
  snprintf(topic_status,   sizeof(topic_status),
           "%s/%s/%s/%s/%s",
           TOPIC_PREFIX, DEVICE_SITE, DEVICE_ASSET, DEVICE_ID, TOPIC_STATUS);
  snprintf(topic_alert,    sizeof(topic_alert),
           "%s/%s/%s/%s/%s",
           TOPIC_PREFIX, DEVICE_SITE, DEVICE_ASSET, DEVICE_ID, TOPIC_ALERT);
  snprintf(topic_cmd,      sizeof(topic_cmd),
           "%s/%s/%s/%s/%s",
           TOPIC_PREFIX, DEVICE_SITE, DEVICE_ASSET, DEVICE_ID, TOPIC_COMMAND);
  snprintf(topic_dlq,      sizeof(topic_dlq),
           "%s/%s/%s/%s/%s",
           TOPIC_PREFIX, DEVICE_SITE, DEVICE_ASSET, DEVICE_ID, TOPIC_DLQ);
}

static void wifi_connect() {
  if (WiFi.status() == WL_CONNECTED) return;
  led_set(LedState::CONNECTING);
  log_i("[WiFi] connecting to %s", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 30000) {
    delay(250);
  }
  if (WiFi.status() == WL_CONNECTED) {
    log_i("[WiFi] connected - IP %s, RSSI %d dBm",
          WiFi.localIP().toString().c_str(), WiFi.RSSI());
    // NTP - get unix time so UUIDv7 gets a real timestamp
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  } else {
    log_e("[WiFi] connect timed out, will retry");
    led_set(LedState::DISCONNECTED);
  }
}

static void mqtt_message_callback(char *topic, byte *payload, unsigned int len) {
  // Null-terminate the payload
  char buf[1024];
  size_t n = (len < sizeof(buf) - 1) ? len : sizeof(buf) - 1;
  memcpy(buf, payload, n);
  buf[n] = '\0';

  log_i("[MQTT] rx %s: %s", topic, buf);

  if (strcmp(topic, topic_cmd) == 0) {
    handle_command(buf);
  } else if (strcmp(topic, topic_alert) == 0) {
    led_set(LedState::ANOMALY);
    log_w("[ALERT] cloud reports anomaly!");
  }
}

static bool mqtt_reconnect() {
  if (WiFi.status() != WL_CONNECTED) return false;

  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setBufferSize(2048);
  mqtt.setKeepAlive(20);
  mqtt.setCallback(mqtt_message_callback);

  // LWT: when we drop, broker publishes "offline" retained on our status topic
  const char *will_payload = "{\"status\":\"offline\",\"reason\":\"lwt\"}";

  log_i("[MQTT] connecting to %s:%d as %s ...", MQTT_HOST, MQTT_PORT, DEVICE_ID);
  bool ok = mqtt.connect(
      DEVICE_ID,
      MQTT_USER,
      MQTT_PASS,
      topic_status,
      1,                  // qos
      true,               // retain
      will_payload,
      true                // clean session
  );
  if (!ok) {
    log_w("[MQTT] connect failed, state=%d", mqtt.state());
    return false;
  }

  // Announce we are online (retained)
  char online_payload[160];
  snprintf(online_payload, sizeof(online_payload),
           "{\"status\":\"online\",\"fw\":\"%s\"}", FIRMWARE_VERSION);
  mqtt.publish(topic_status, online_payload, true);

  // Subscribe to cmd + alert topics
  mqtt.subscribe(topic_cmd, 1);
  mqtt.subscribe(topic_alert, 1);

  log_i("[MQTT] connected; subscribed cmd + alert");
  led_set(LedState::HEALTHY);
  connected_flag = true;
  return true;
}

// ---------- Queue API ----------

void mqtt_enqueue_feature(const FeatureVector &f) {
  portENTER_CRITICAL(&q_mux);
  uint8_t next = (q_head + 1) % FEATURE_QUEUE_SIZE;
  if (next == q_tail) {
    // Full - drop oldest
    q_tail = (q_tail + 1) % FEATURE_QUEUE_SIZE;
  }
  feature_queue[q_head] = f;
  q_head = next;
  portEXIT_CRITICAL(&q_mux);
}

static bool dequeue_feature(FeatureVector &out) {
  portENTER_CRITICAL(&q_mux);
  if (q_head == q_tail) {
    portEXIT_CRITICAL(&q_mux);
    return false;
  }
  out = feature_queue[q_tail];
  q_tail = (q_tail + 1) % FEATURE_QUEUE_SIZE;
  portEXIT_CRITICAL(&q_mux);
  return true;
}

// ---------- Serialization ----------

static void serialize_feature(const FeatureVector &f, char *out, size_t out_size) {
  JsonDocument doc;
  char msg_id[37];
  uuidv7(msg_id);

  doc["schema_version"] = SCHEMA_VERSION;
  doc["msg_id"]         = msg_id;
  doc["device_id"]      = DEVICE_ID;
  doc["site"]           = DEVICE_SITE;
  doc["asset"]          = DEVICE_ASSET;
  doc["fw"]             = FIRMWARE_VERSION;
  doc["seq"]            = f.seq;
  doc["quality"]        = f.quality;

  // ISO-8601 UTC timestamp from unix_ms
  time_t now = time(nullptr);
  struct tm tm_utc;
  gmtime_r(&now, &tm_utc);
  char ts[32];
  strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", &tm_utc);
  doc["timestamp"] = ts;

  JsonObject m = doc["metrics"].to<JsonObject>();
  // ---- Fault injection hooks ----
  if (g_inject_null) {
    m["rms_g"] = nullptr;
    g_inject_null = false;
  } else if (g_inject_oor) {
    m["rms_g"] = 99.9;       // out of Pydantic le=32.0 bound
    g_inject_oor = false;
  } else {
    m["rms_g"] = f.rms_g;
  }
  m["kurtosis"] = f.kurtosis;
  m["crest"]    = f.crest_factor;
  m["peak_hz"]  = f.peak_hz;
  m["peak_mag"] = f.peak_mag;

  JsonArray mel = m["mel"].to<JsonArray>();
  for (int i = 0; i < 6; i++) mel.add(f.mel[i]);

  m["therm_min_c"]  = f.therm_min_c;
  m["therm_mean_c"] = f.therm_mean_c;
  m["therm_max_c"]  = f.therm_max_c;
  m["accel_x_rms"]  = f.accel_x_rms;
  m["accel_y_rms"]  = f.accel_y_rms;
  m["accel_mag_rms"]= f.accel_mag_rms;
  m["therm_gradient_c"]  = f.therm_gradient_c;
  m["therm_hotspot_pct"] = f.therm_hotspot_pct;

  if (g_inject_schema_bad) {
    doc["schema_version"] = "0.9";   // breaks Literal["1.0"] check
    g_inject_schema_bad = false;
  }

  serializeJson(doc, out, out_size);
}

// ---------- Task ----------

bool mqtt_is_connected() {
  return connected_flag && mqtt.connected();
}

void mqtt_publish_raw(const char *topic, const char *payload, bool retained) {
  if (!mqtt_is_connected()) return;
  mqtt.publish(topic, payload, retained);
}

void mqtt_init() {
  build_topics();
  wifi_connect();
}

void mqtt_task(void *param) {
  uint32_t last_status_ms = 0;

  for (;;) {
    if (WiFi.status() != WL_CONNECTED) {
      connected_flag = false;
      wifi_connect();
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }

    if (!mqtt.connected()) {
      connected_flag = false;
      led_set(LedState::CONNECTING);
      if (!mqtt_reconnect()) {
        vTaskDelay(pdMS_TO_TICKS(2000));
        continue;
      }
    }

    mqtt.loop();   // services callbacks + keepalive

    // Drain feature queue
    FeatureVector f;
    while (dequeue_feature(f)) {
      char json[1536];
      serialize_feature(f, json, sizeof(json));
      bool ok = mqtt.publish(topic_features, json, false);
      if (!ok) {
        log_w("[MQTT] publish failed (state=%d), will reconnect", mqtt.state());
        break;
      }
    }

    // Periodic status heartbeat
    uint32_t now_ms = millis();
    if (now_ms - last_status_ms > STATUS_HEARTBEAT_SEC * 1000UL) {
      char status_payload[160];
      snprintf(status_payload, sizeof(status_payload),
               "{\"status\":\"online\",\"fw\":\"%s\",\"uptime_s\":%lu}",
               FIRMWARE_VERSION, (unsigned long)(millis() / 1000));
      mqtt.publish(topic_status, status_payload, true);
      last_status_ms = now_ms;
    }

    vTaskDelay(pdMS_TO_TICKS(50));
  }
}
