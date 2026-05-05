// I2C smoke test - scans the bus and prints any device addresses found.
// Expected: "Found device at 0x68" (the MPU-9250).
// If you also wire MLX90640 or INMP441 later, those will show up here too.

#include <Arduino.h>
#include <Wire.h>

#define PIN_SDA 8
#define PIN_SCL 9

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("\n\n========================================");
  Serial.println("I2C smoke test - scanning bus 0x01..0x7F");
  Serial.printf("SDA=GPIO%d  SCL=GPIO%d\n", PIN_SDA, PIN_SCL);
  Serial.println("========================================\n");
  pinMode(PIN_SDA, INPUT_PULLUP);
  pinMode(PIN_SCL, INPUT_PULLUP);
  delay(10);
  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.setClock(100000);  // 100 kHz - slow + forgiving
}

void loop() {
  int found = 0;
  for (byte addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("  Found device at 0x%02X\n", addr);
      found++;
    }
  }
  if (found == 0) Serial.println("  No I2C devices found.");
  Serial.printf("Scan complete (%d found). Repeating in 3s...\n\n", found);
  delay(3000);
}
