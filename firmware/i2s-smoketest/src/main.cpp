// I2S smoke test - reads INMP441, computes RMS audio level every 200ms.
// Tap or speak into the mic - you should see RMS jump from low to high.

#include <Arduino.h>
#include "driver/i2s.h"
#include <math.h>

// INMP441 pin assignments (matches sensor-node firmware)
#define PIN_I2S_WS   4
#define PIN_I2S_SCK  5
#define PIN_I2S_SD   6

#define SAMPLE_RATE  16000
#define SAMPLE_COUNT 1024

static int32_t buffer[SAMPLE_COUNT];

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("\n\n========================================");
  Serial.println("I2S smoke test - INMP441 microphone");
  Serial.printf("WS=GPIO%d  SCK=GPIO%d  SD=GPIO%d\n", PIN_I2S_WS, PIN_I2S_SCK, PIN_I2S_SD);
  Serial.println("Tap or speak near the mic. RMS should rise.");
  Serial.println("========================================\n");

  i2s_config_t cfg = {
    .mode              = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate       = SAMPLE_RATE,
    .bits_per_sample   = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format    = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags  = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count     = 8,
    .dma_buf_len       = 256,
    .use_apll          = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk        = 0
  };
  i2s_driver_install(I2S_NUM_0, &cfg, 0, NULL);

  i2s_pin_config_t pins = {
    .mck_io_num   = I2S_PIN_NO_CHANGE,
    .bck_io_num   = PIN_I2S_SCK,
    .ws_io_num    = PIN_I2S_WS,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num  = PIN_I2S_SD,
  };
  i2s_set_pin(I2S_NUM_0, &pins);
  i2s_start(I2S_NUM_0);
}

void loop() {
  size_t bytes_read = 0;
  esp_err_t err = i2s_read(I2S_NUM_0, buffer, sizeof(buffer), &bytes_read, pdMS_TO_TICKS(500));

  if (err != ESP_OK) {
    Serial.printf("I2S read error: %d\n", err);
    delay(500);
    return;
  }

  size_t n = bytes_read / sizeof(int32_t);
  if (n == 0) {
    Serial.println("No samples read. Check WS/SCK/SD wiring.");
    delay(500);
    return;
  }

  // Compute RMS amplitude. INMP441 outputs 24-bit data in upper bits of int32.
  double sum_sq = 0;
  int32_t mn = INT32_MAX, mx = INT32_MIN;
  for (size_t i = 0; i < n; i++) {
    int32_t s = buffer[i] >> 14;  // bring 24-bit signal into reasonable range
    sum_sq += (double)s * (double)s;
    if (s < mn) mn = s;
    if (s > mx) mx = s;
  }
  double rms = sqrt(sum_sq / n);

  Serial.printf("RMS=%.0f  min=%d  max=%d  span=%d  samples=%u\n",
                rms, mn, mx, mx - mn, (unsigned)n);

  delay(200);
}
