// imu_task.cpp - MPU-9250 via Bolder Flight library
// Samples Z-axis accelerometer at 250 Hz into a ring buffer for FFT.

#include "imu_task.h"
#include "config.h"
#include "MPU9250.h"
#include <Wire.h>

MPU9250 imu(Wire, MPU9250_I2C_ADDR);
ImuRingBuffer g_imu_ring = {{0}, 0, 0};
volatile bool g_imu_ok = false;

void imu_init() {
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.setClock(I2C_CLOCK_HZ);

  if (g_i2c_mutex) xSemaphoreTake(g_i2c_mutex, portMAX_DELAY);
  int status = imu.begin();
  for (int attempt = 0; attempt < 5 && status < 0; attempt++) {
    log_w("[IMU] begin() failed (status=%d) - retrying...", status);
    delay(500);
    status = imu.begin();
  }
  if (g_i2c_mutex) xSemaphoreGive(g_i2c_mutex);

  if (status < 0) {
    log_e("[IMU] FATAL: MPU-9250 init failed (status=%d).", status);
    return;
  }

  if (g_i2c_mutex) xSemaphoreTake(g_i2c_mutex, portMAX_DELAY);
  imu.setAccelRange(MPU9250::ACCEL_RANGE_4G);
  imu.setGyroRange(MPU9250::GYRO_RANGE_500DPS);
  imu.setDlpfBandwidth(MPU9250::DLPF_BANDWIDTH_184HZ);
  imu.setSrd(0);
  if (g_i2c_mutex) xSemaphoreGive(g_i2c_mutex);

  g_imu_ok = true;
  log_i("[IMU] MPU-9250 initialized (+/-4g, DLPF 184Hz, 250 Hz sample)");
}

void imu_task(void *param) {
  if (!g_imu_ok) {
    log_w("[IMU] task exiting - sensor not initialized");
    vTaskDelete(NULL);
    return;
  }

  const TickType_t period = pdMS_TO_TICKS(4);  // 250 Hz
  TickType_t last_wake = xTaskGetTickCount();

  uint32_t last_heartbeat = millis();
  for (;;) {
    vTaskDelayUntil(&last_wake, period);

    // Take I2C mutex for this single read
    if (xSemaphoreTake(g_i2c_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
      /* skip silently */
      continue;
    }
    imu.readSensor();
    float z_g = imu.getAccelZ_mss() / 9.80665f;
    xSemaphoreGive(g_i2c_mutex);

    uint32_t idx = g_imu_ring.write_index;
    g_imu_ring.samples[idx] = z_g;
    g_imu_ring.write_index = (idx + 1) % IMU_FFT_SIZE;
    g_imu_ring.total_samples++;

    if (millis() - last_heartbeat > 5000) {
      log_i("[IMU] heartbeat: total_samples=%u  last_z=%.4f g", g_imu_ring.total_samples, z_g);
      last_heartbeat = millis();
    }
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
