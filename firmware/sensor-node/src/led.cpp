#include "led.h"
#include "config.h"

static volatile LedState current_state = LedState::BOOTING;

static inline void write_channel(int pin, uint8_t value) {
  analogWrite(pin, LED_ACTIVE_LOW ? (255 - value) : value);
}

static inline void rgb(uint8_t r, uint8_t g, uint8_t b) {
  write_channel(PIN_LED_R, r);
  write_channel(PIN_LED_G, g);
  write_channel(PIN_LED_B, b);
}

void led_init() {
  pinMode(PIN_LED_R, OUTPUT);
  pinMode(PIN_LED_G, OUTPUT);
  pinMode(PIN_LED_B, OUTPUT);
  rgb(0, 0, 0);
}

void led_set(LedState s) {
  current_state = s;
}

void led_task(void *param) {
  uint32_t phase = 0;
  for (;;) {
    LedState s = current_state;
    phase++;
    switch (s) {
      case LedState::BOOTING: {
        uint8_t v = (uint8_t)((sin(phase * 0.05f) + 1.0f) * 127.5f);
        rgb(v, 0, v);
        vTaskDelay(pdMS_TO_TICKS(30));
        break;
      }
      case LedState::CONNECTING: {
        uint8_t v = (uint8_t)((sin(phase * 0.1f) + 1.0f) * 127.5f);
        rgb(v, v, 0);
        vTaskDelay(pdMS_TO_TICKS(20));
        break;
      }
      case LedState::HEALTHY:
        rgb(0, 180, 0);
        vTaskDelay(pdMS_TO_TICKS(100));
        break;
      case LedState::ANOMALY:
        rgb(220, 0, 0);
        vTaskDelay(pdMS_TO_TICKS(100));
        break;
      case LedState::DISCONNECTED:
        rgb((phase % 10 < 5) ? 200 : 0, 0, 0);
        vTaskDelay(pdMS_TO_TICKS(100));
        break;
      case LedState::FAULT_INJECTED:
        rgb(0, 200, 200);
        vTaskDelay(pdMS_TO_TICKS(200));
        rgb(0, 0, 0);
        vTaskDelay(pdMS_TO_TICKS(200));
        break;
    }
  }
}
