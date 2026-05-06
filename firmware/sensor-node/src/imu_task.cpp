// imu_task.cpp - MPU-9250 via Bolder Flight library
// Samples X/Y/Z accelerometer at 100 Hz into ring buffers for analysis.

#include "imu_task.h"
#include "config.h"
#include "MPU9250.h"
#include <Wire.h>

MPU9250 imu(Wire, MPU9250_I2C_ADDR);
ImuRingBuffer g_imu_ring = {{0}, {0}, {0}, 0, 0};
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
  log_i("[IMU] MPU-9250 initialized (XYZ +/-4g, DLPF 184Hz, 100 Hz sample)");
}

void imu_task(void *param) {
  if (!g_imu_ok) {
    log_w("[IMU] task exiting - sensor not initialized");
    vTaskDelete(NULL);
    return;
  }

  const TickType_t period = pdMS_TO_TICKS(10);  // 100 Hz
  TickType_t last_wake = xTaskGetTickCount();
  uint32_t last_heartbeat = millis();

  for (;;) {
    vTaskDelayUntil(&last_wake, period);

    if (xSemaphoreTake(g_i2c_mutex, pdMS_TO_TICKS(500)) != pdTRUE) continue;
    imu.readSensor();
    float ax = imu.getAccelX_mss() / 9.80665f;
    float ay = imu.getAccelY_mss() / 9.80665f;
    float az = imu.getAccelZ_mss() / 9.80665f;
    xSemaphoreGive(g_i2c_mutex);

    uint32_t idx = g_imu_ring.write_index;
    g_imu_ring.samples[idx]   = az;
    g_imu_ring.samples_x[idx] = ax;
    g_imu_ring.samples_y[idx] = ay;
    g_imu_ring.write_index = (idx + 1) % IMU_FFT_SIZE;
    g_imu_ring.total_samples++;

    if (millis() - last_heartbeat > 5000) {
      log_i("[IMU] hb: total=%u  X=%.3f Y=%.3f Z=%.3f g",
            g_imu_ring.total_samples, ax, ay, az);
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

bool imu_snapshot_xyz(float *x, float *y, float *z, size_t n) {
  if (g_imu_ring.total_samples < n) return false;
  uint32_t end = g_imu_ring.write_index;
  uint32_t start = (end + IMU_FFT_SIZE - n) % IMU_FFT_SIZE;
  for (size_t i = 0; i < n; i++) {
    uint32_t k = (start + i) % IMU_FFT_SIZE;
    x[i] = g_imu_ring.samples_x[k];
    y[i] = g_imu_ring.samples_y[k];
    z[i] = g_imu_ring.samples[k];
  }
  return true;
}
