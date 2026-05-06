// thermal_task.cpp - MLX90640 32x24 thermal camera at 4 Hz.

#include "thermal_task.h"
#include "config.h"
#include <Wire.h>
#include <Adafruit_MLX90640.h>

static Adafruit_MLX90640 mlx;
static float frame_buf[768];

volatile bool  g_thermal_ready = false;
volatile float g_thermal_min_c  = 0;
volatile float g_thermal_mean_c = 0;
volatile float g_thermal_max_c  = 0;
volatile float g_thermal_hotspot_pct = 0;

void thermal_init() {
  if (g_i2c_mutex) xSemaphoreTake(g_i2c_mutex, portMAX_DELAY);
  bool ok = mlx.begin(MLX90640_I2CADDR_DEFAULT, &Wire);
  if (ok) {
    mlx.setMode(MLX90640_CHESS);
    mlx.setResolution(MLX90640_ADC_18BIT);
    mlx.setRefreshRate(MLX90640_4_HZ);
    Wire.setClock(1000000);
    log_i("[THERMAL] MLX90640 initialized @ 4 Hz, 18-bit");
  } else {
    log_e("[THERMAL] MLX90640 init failed");
  }
  if (g_i2c_mutex) xSemaphoreGive(g_i2c_mutex);
}

void thermal_task(void *param) {
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(250));

    // Take mutex for the whole frame read - this is critical, frames are 1500+ bytes
    if (xSemaphoreTake(g_i2c_mutex, pdMS_TO_TICKS(500)) != pdTRUE) continue;
    int rc = mlx.getFrame(frame_buf);
    xSemaphoreGive(g_i2c_mutex);

    if (rc != 0) {
      log_w("[THERMAL] frame read failed (%d)", rc);
      continue;
    }

    float mn = 1e9f, mx = -1e9f, sum = 0;
    for (int i = 0; i < 768; i++) {
      float t = frame_buf[i];
      if (t < mn) mn = t;
      if (t > mx) mx = t;
      sum += t;
    }
    float mean_t = sum / 768.0f;
    // Compute std dev in second pass, then count pixels > mean + 2*std
    float ss = 0;
    for (int i = 0; i < 768; i++) {
      float d = frame_buf[i] - mean_t;
      ss += d*d;
    }
    float std_t = sqrtf(ss / 768.0f);
    float threshold = mean_t + 2.0f * std_t;
    int hotspot_count = 0;
    for (int i = 0; i < 768; i++) {
      if (frame_buf[i] > threshold) hotspot_count++;
    }

    g_thermal_min_c  = mn;
    g_thermal_mean_c = mean_t;
    g_thermal_max_c  = mx;
    g_thermal_hotspot_pct = (100.0f * hotspot_count) / 768.0f;
    g_thermal_ready  = true;
  }
}
