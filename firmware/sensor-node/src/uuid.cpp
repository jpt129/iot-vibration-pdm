// UUIDv7 generator - RFC 9562. High 48 bits = unix_ms.
#include "uuid.h"
#include <time.h>
#include <esp_random.h>

void uuidv7(char *buf) {
  uint8_t b[16];
  uint64_t ms = (uint64_t)time(nullptr) * 1000ULL;
  ms += (micros() / 1000) % 1000;

  b[0] = (ms >> 40) & 0xFF;
  b[1] = (ms >> 32) & 0xFF;
  b[2] = (ms >> 24) & 0xFF;
  b[3] = (ms >> 16) & 0xFF;
  b[4] = (ms >> 8)  & 0xFF;
  b[5] = ms & 0xFF;

  for (int i = 6; i < 16; i++) b[i] = esp_random() & 0xFF;

  b[6] = (b[6] & 0x0F) | 0x70;
  b[8] = (b[8] & 0x3F) | 0x80;

  snprintf(buf, 37,
           "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
           b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7],
           b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
}
