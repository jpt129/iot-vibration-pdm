#pragma once
#include <Arduino.h>

struct ImuRingBuffer {
  float samples[1024];
  volatile uint32_t write_index;
  volatile uint32_t total_samples;
};

extern ImuRingBuffer g_imu_ring;

void imu_init();
void imu_task(void *param);
bool imu_snapshot(float *out, size_t n);
