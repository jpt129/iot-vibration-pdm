#pragma once
#include <Arduino.h>
#include "features.h"

void mqtt_init();
void mqtt_task(void *param);

// Producer: enqueue a feature vector for publishing on the next loop tick.
// Non-blocking - drops the oldest if queue is full.
void mqtt_enqueue_feature(const FeatureVector &f);

// Direct publish helper (used by fault_injection for malformed payloads).
void mqtt_publish_raw(const char *topic, const char *payload, bool retained);

// True if MQTT broker connection is currently up.
bool mqtt_is_connected();
