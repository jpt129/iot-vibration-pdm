// imu_task.cpp - MPU-9250 via Bolder Flight library
// Samples Z-axis accelerometer at 1 kHz into a ring buffer for FFT.

#include "imu_task.h"
#include "config.h"
#include "MPU9250.h"
#include <Wire.h>

MPU9250 imu(Wire, MPU9250_I2C_ADDR);
ImuRingBuffer g_imu_ring = {{0}, 0, 0};

void imu_init() {
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.setClock(I2C_CLOCK_HZ);

  int status = imu.begin();
  for (int attempt = 0; attempt < 5 && status < 0; attempt++) {
    log_w("[IMU] begin() failed (status=%d) - retrying...", status);
    delay(500);
    status = imu.begin();
  }
  if (status < 0) {
    log_e("[IMU] FATAL: MPU-9250 init failed (status=%d). "
          "Check wiring SDA=%d SCL=%d, address 0x%02X.",
          status, PIN_I2C_SDA, PIN_I2C_SCL, MPU9250_I2C_ADDR);
    return;
  }

  imu.setAccelRange(MPU9250::ACCEL_RANGE_4G);
  imu.setGyroRange(MPU9250::GYRO_RANGE_500DPS);
  imu.setDlpfBandwidth(MPU9250::DLPF_BANDWIDTH_184HZ);
  imu.setSrd(0);

  log_i("[IMU] MPU-9250 initialized (+/-4g, DLPF 184Hz, 1 kHz)");
}

void imu_task(void *param) {
  const uint32_t period_us = 1000000UL / IMU_SAMPLE_HZ;
  uint32_t next_sample = micros();

  for (;;) {
    while ((int32_t)(micros() - next_sample) < 0) { /* spin */ }
    next_sample += period_us;

    imu.readSensor();
    float z_g = imu.getAccelZ_mss() / 9.80665f;

    uint32_t idx = g_imu_ring.write_index;
    g_imu_ring.samples[idx] = z_g;
    g_imu_ring.write_index = (idx + 1) % IMU_FFT_SIZE;
    g_imu_ring.total_samples++;

    if ((g_imu_ring.total_samples & 0x3F) == 0) taskYIELD();
  }
}

bool imu_snapshot(float *out, size_t n) {
  if (g_imu_ring.total_samples < n) return false;
  uint32_t end = g_imu_ring.write_index;
  uint32_t start = (end + IMU_FFT_SIZE - n) % IMU_FFT_SIZE;
  for (size_t i = 0; i < n; i++) {
    out[i] = g_imu_ring.samples[(start + i) % IMU_FFT_SIZE];
  }
  return true;
}
