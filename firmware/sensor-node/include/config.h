// config.h - all compile-time tuning
#pragma once
#include <Arduino.h>

#define FIRMWARE_VERSION   "1.0.0"
#define SCHEMA_VERSION     "1.0"
#define DEVICE_SITE        "lab"
#define DEVICE_ASSET       "fan01"

#define TOPIC_PREFIX       "iotpdm"
#define TOPIC_FEATURES     "features"
#define TOPIC_BURST        "burst"
#define TOPIC_STATUS       "status"
#define TOPIC_ALERT        "alert"
#define TOPIC_COMMAND      "cmd"
#define TOPIC_DLQ          "dlq"

#define IMU_SAMPLE_HZ           1000
#define IMU_FFT_SIZE            1024
#define IMU_FFT_BINS            (IMU_FFT_SIZE / 2)

#define MIC_SAMPLE_HZ           16000
#define MIC_BUFFER_SAMPLES      512

#define THERMAL_REFRESH_HZ      4

#define FEATURE_PUBLISH_HZ      1
#define BURST_PUBLISH_SEC       60
#define STATUS_HEARTBEAT_SEC    15

#define PIN_I2C_SDA        8
#define PIN_I2C_SCL        9
#define I2C_CLOCK_HZ       400000

#define PIN_I2S_WS         4
#define PIN_I2S_SCK        5
#define PIN_I2S_SD         6
#define I2S_PORT_NUM       I2S_NUM_0

#define PIN_LED_R          15
#define PIN_LED_G          16
#define PIN_LED_B          17
#define LED_ACTIVE_LOW     true

#define MPU9250_I2C_ADDR   0x68

#define BOUND_RMS_G_MAX    32.0f
#define BOUND_PEAK_HZ_MIN  0.0f
#define BOUND_PEAK_HZ_MAX  500.0f
#define BOUND_TEMP_C_MIN   -20.0f
#define BOUND_TEMP_C_MAX   120.0f

#define TASK_STACK_IMU       8192
#define TASK_STACK_MIC       8192
#define TASK_STACK_THERMAL   8192
#define TASK_STACK_FEATURES  16384
#define TASK_STACK_MQTT      12288

#define TASK_PRIO_IMU        5
#define TASK_PRIO_MIC        4
#define TASK_PRIO_THERMAL    3
#define TASK_PRIO_FEATURES   2
#define TASK_PRIO_MQTT       1

#define CORE_SENSORS         0
#define CORE_NETWORKING      1
