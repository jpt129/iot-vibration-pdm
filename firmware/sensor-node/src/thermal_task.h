#pragma once
#include <Arduino.h>

extern volatile float g_thermal_max_c;
extern volatile float g_thermal_mean_c;
extern volatile float g_thermal_min_c;
extern volatile bool  g_thermal_ready;

void thermal_init();
void thermal_task(void *param);
