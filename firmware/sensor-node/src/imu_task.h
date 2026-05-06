#pragma once
#include <Arduino.h>

struct ImuRingBuffer {
  float samples[1024];      // Z-axis (legacy, used by FFT)
  float samples_x[1024];    // X-axis
  float samples_y[1024];    // Y-axis
  volatile uint32_t write_index;
  volatile uint32_t total_samples;
};

extern ImuRingBuffer g_imu_ring;
extern volatile bool g_imu_ok;

void imu_init();
void imu_task(void *param);
bool imu_snapshot(float *out, size_t n);
bool imu_snapshot_xyz(float *x, float *y, float *z, size_t n);
