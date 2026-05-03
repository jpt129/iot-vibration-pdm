// Demo fault-injection: cmd topic -> publish bad data on next round.

#include "fault_injection.h"
#include "config.h"
#include "secrets.h"
#include "mqtt_publisher.h"
#include "led.h"
#include <ArduinoJson.h>

volatile bool g_inject_null       = false;
volatile bool g_inject_oor        = false;
volatile bool g_inject_schema_bad = false;

void handle_command(const char *json_str) {
  JsonDocument doc;
  if (deserializeJson(doc, json_str) != DeserializationError::Ok) {
    log_w("[CMD] malformed: %s", json_str);
    return;
  }
  const char *cmd = doc["cmd"] | "";
  log_i("[CMD] received: %s", cmd);

  if      (strcmp(cmd, "inject_null")      == 0) g_inject_null = true;
  else if (strcmp(cmd, "inject_oor")       == 0) g_inject_oor = true;
  else if (strcmp(cmd, "inject_schema")    == 0) g_inject_schema_bad = true;
  else if (strcmp(cmd, "inject_malformed") == 0) {
    char topic[128];
    snprintf(topic, sizeof(topic), "%s/%s/%s/%s/%s",
             TOPIC_PREFIX, DEVICE_SITE, DEVICE_ASSET, DEVICE_ID, TOPIC_FEATURES);
    mqtt_publish_raw(topic, "this is not json at all", false);
    led_set(LedState::FAULT_INJECTED);
  }
  else if (strcmp(cmd, "reboot") == 0) {
    log_w("[CMD] rebooting per remote command");
    delay(500);
    ESP.restart();
  }
}
