// MLX90640 thermal camera @ 4 Hz -> min/mean/max over 768 pixels.

#include "thermal_task.h"
#include "config.h"
#include <Adafruit_MLX90640.h>

Adafruit_MLX90640 mlx;
static float frame[32*24];

volatile float g_thermal_max_c  = 0;
volatile float g_thermal_mean_c = 0;
volatile float g_thermal_min_c  = 0;
volatile bool  g_thermal_ready  = false;

void thermal_init() {
  if (!mlx.begin(MLX90640_I2CADDR_DEFAULT, &Wire)) {
    log_e("[THERMAL] MLX90640 not found on I2C bus!");
    return;
  }
  mlx.setMode(MLX90640_CHESS);
  mlx.setResolution(MLX90640_ADC_18BIT);
  mlx.setRefreshRate(MLX90640_4_HZ);
  Wire.setClock(1000000);
  log_i("[THERMAL] MLX90640 initialized @ 4 Hz, 18-bit");
}

void thermal_task(void *param) {
  for (;;) {
    if (mlx.getFrame(frame) != 0) {
      log_w("[THERMAL] frame read failed");
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }

    float mn = 1000.0f, mx = -1000.0f, sum = 0.0f;
    for (int i = 0; i < 768; i++) {
      float t = frame[i];
      if (t < mn) mn = t;
      if (t > mx) mx = t;
      sum += t;
    }

    g_thermal_min_c  = mn;
    g_thermal_max_c  = mx;
    g_thermal_mean_c = sum / 768.0f;
    g_thermal_ready  = true;

    vTaskDelay(pdMS_TO_TICKS(50));
  }
}
