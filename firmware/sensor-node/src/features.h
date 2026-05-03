#pragma once
#include <Arduino.h>

struct FeatureVector {
  uint32_t seq;
  uint32_t unix_ms;

  float rms_g;
  float kurtosis;
  float crest_factor;
  float peak_hz;
  float peak_mag;

  float mel[6];

  float therm_min_c;
  float therm_mean_c;
  float therm_max_c;

  uint8_t quality;
};

void features_init();
void features_task(void *param);
