#pragma once
#include <Arduino.h>

enum class LedState {
  BOOTING, CONNECTING, HEALTHY, ANOMALY, DISCONNECTED, FAULT_INJECTED
};

void led_init();
void led_set(LedState s);
void led_task(void *param);
