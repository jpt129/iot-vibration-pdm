// MLX90640 thermal camera smoke test
// Reads 32x24 thermal frames, prints min/mean/max temp every ~250ms.
// Wave your hand in front of the lens - max temp should jump 5-15 degC.

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_MLX90640.h>

#define PIN_SDA 8
#define PIN_SCL 9

Adafruit_MLX90640 mlx;
float frame[32 * 24];

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("\n\n========================================");
  Serial.println("MLX90640 thermal camera smoke test");
  Serial.println("========================================\n");

  Wire.begin(PIN_SDA, PIN_SCL);

  if (!mlx.begin(MLX90640_I2CADDR_DEFAULT, &Wire)) {
    Serial.println("FATAL: MLX90640 not found on I2C bus.");
    while (1) delay(1000);
  }

  Serial.printf("Serial #: %x %x %x\n", mlx.serialNumber[0], mlx.serialNumber[1], mlx.serialNumber[2]);
  mlx.setMode(MLX90640_CHESS);
  mlx.setResolution(MLX90640_ADC_18BIT);
  mlx.setRefreshRate(MLX90640_4_HZ);
  Wire.setClock(1000000);  // bump to 1MHz for fast frame xfer

  Serial.println("Initialized. Reading frames...\n");
}

void loop() {
  if (mlx.getFrame(frame) != 0) {
    Serial.println("frame read failed");
    delay(250);
    return;
  }

  float mn = 1e9, mx = -1e9, sum = 0;
  for (int i = 0; i < 768; i++) {
    if (frame[i] < mn) mn = frame[i];
    if (frame[i] > mx) mx = frame[i];
    sum += frame[i];
  }
  float mean = sum / 768.0f;

  // Coarse "ASCII heatmap" of the center column to make hot spots visible
  int center_col = 16;
  char ramp[] = " .:-=+*#%@";
  Serial.printf("min=%5.1fC  mean=%5.1fC  max=%5.1fC   col16: ", mn, mean, mx);
  for (int row = 0; row < 24; row++) {
    float t = frame[row * 32 + center_col];
    int idx = (int)((t - mn) / (mx - mn + 0.01) * 9);
    if (idx < 0) idx = 0;
    if (idx > 9) idx = 9;
    Serial.print(ramp[idx]);
  }
  Serial.println();

  delay(250);
}
