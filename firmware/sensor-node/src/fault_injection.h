#pragma once
#include <Arduino.h>

extern volatile bool g_inject_null;
extern volatile bool g_inject_oor;
extern volatile bool g_inject_schema_bad;

void handle_command(const char *json_str);
