#pragma once
#include <Arduino.h>

extern volatile float g_mel_energies[6];
extern volatile bool  g_mel_ready;

void mic_init();
void mic_task(void *param);
