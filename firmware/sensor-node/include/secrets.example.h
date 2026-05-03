// secrets.example.h - COMMIT THIS; copy to secrets.h and fill in.
// secrets.h is .gitignored.
#pragma once

#define WIFI_SSID      "YOUR-WIFI-SSID"
#define WIFI_PASSWORD  "YOUR-WIFI-PASSWORD"

// MQTT broker - currently pointing at Mac dev machine
// For Pi (home WiFi):  10.0.0.252
// For Pi (AP mode):    192.168.50.1
#define MQTT_HOST      "10.0.0.33"
#define MQTT_PORT      1883
#define MQTT_USER      "esp32-a17"
#define MQTT_PASS      "node-secret-change-me"

#define DEVICE_ID      "esp32-a17"
