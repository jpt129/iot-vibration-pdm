#pragma once
#include <Arduino.h>

// Generate a UUIDv7 string. Caller must provide a 37-byte buffer (36 chars + null).
void uuidv7(char *buf);
