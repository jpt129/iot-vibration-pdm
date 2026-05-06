// main.cpp - boot, init all sensors, launch FreeRTOS tasks

#include <Arduino.h>
#include "config.h"
#include "secrets.h"
#include "imu_task.h"
#include "mic_task.h"
#include "thermal_task.h"
#include "features.h"
#include "mqtt_publisher.h"
#include "led.h"

SemaphoreHandle_t g_i2c_mutex = nullptr;

void setup() {
  g_i2c_mutex = xSemaphoreCreateMutex();
  Serial.begin(115200);
  delay(2000);
  Serial.println("\n\n=========================================");
  Serial.printf ("IoT Vibration PdM - %s  fw %s\n", DEVICE_ID, FIRMWARE_VERSION);
  Serial.println("=========================================");

  led_init();
  led_set(LedState::BOOTING);

  // Sensors share I2C bus (MPU at 0x68, MLX at 0x33)
  imu_init();
  delay(100);   // small gap before bringing MLX up on the same bus
  thermal_init();

  mic_init();

  features_init();
  mqtt_init();

  // Launch FreeRTOS tasks pinned to specific cores
  xTaskCreatePinnedToCore(led_task,      "led",      4096,                NULL, 1,                  NULL, CORE_NETWORKING);
  xTaskCreatePinnedToCore(imu_task,      "imu",      TASK_STACK_IMU,      NULL, TASK_PRIO_IMU,      NULL, CORE_SENSORS);
  xTaskCreatePinnedToCore(mic_task,      "mic",      TASK_STACK_MIC,      NULL, TASK_PRIO_MIC,      NULL, CORE_SENSORS);
  xTaskCreatePinnedToCore(thermal_task,  "thermal",  TASK_STACK_THERMAL,  NULL, TASK_PRIO_THERMAL,  NULL, CORE_SENSORS);
  xTaskCreatePinnedToCore(features_task, "features", TASK_STACK_FEATURES, NULL, TASK_PRIO_FEATURES, NULL, CORE_SENSORS);
  xTaskCreatePinnedToCore(mqtt_task,     "mqtt",     TASK_STACK_MQTT,     NULL, TASK_PRIO_MQTT,     NULL, CORE_NETWORKING);

  Serial.printf("All tasks launched. Free heap: %u  PSRAM free: %u\n",
                (unsigned)ESP.getFreeHeap(),
                (unsigned)ESP.getFreePsram());
}

void loop() {
  // FreeRTOS does the real work. Keep the loop alive but idle.
  vTaskDelay(pdMS_TO_TICKS(1000));
}
