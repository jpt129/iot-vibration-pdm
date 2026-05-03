// INMP441 I2S mic, 16 kHz -> 6 log-Mel-like band energies, smoothed.

#include "mic_task.h"
#include "config.h"
#include "driver/i2s.h"
#include <arduinoFFT.h>

volatile float g_mel_energies[6] = {0};
volatile bool  g_mel_ready = false;

static int32_t  raw_buffer[MIC_BUFFER_SAMPLES];
static double   fft_real[512];
static double   fft_imag[512];
static ArduinoFFT<double> FFT = ArduinoFFT<double>(fft_real, fft_imag, 512, MIC_SAMPLE_HZ);

static const float band_edges[7] = {100, 400, 800, 1600, 3200, 6400, 8000};

void mic_init() {
  const i2s_config_t cfg = {
    .mode              = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate       = MIC_SAMPLE_HZ,
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
  i2s_driver_install(I2S_PORT_NUM, &cfg, 0, NULL);

  const i2s_pin_config_t pins = {
    .mck_io_num   = I2S_PIN_NO_CHANGE,
    .bck_io_num   = PIN_I2S_SCK,
    .ws_io_num    = PIN_I2S_WS,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num  = PIN_I2S_SD,
  };
  i2s_set_pin(I2S_PORT_NUM, &pins);
  i2s_start(I2S_PORT_NUM);
  log_i("[MIC] INMP441 I2S started @ %d Hz", MIC_SAMPLE_HZ);
}

void mic_task(void *param) {
  size_t bytes_read;
  float  mels[6];

  for (;;) {
    esp_err_t err = i2s_read(I2S_PORT_NUM, raw_buffer,
                             MIC_BUFFER_SAMPLES * sizeof(int32_t),
                             &bytes_read, pdMS_TO_TICKS(100));
    if (err != ESP_OK || bytes_read < MIC_BUFFER_SAMPLES * sizeof(int32_t)) continue;

    for (int i = 0; i < 512; i++) {
      fft_real[i] = (double)(raw_buffer[i] >> 14);
      fft_imag[i] = 0.0;
    }

    FFT.windowing(FFTWindow::Hamming, FFTDirection::Forward);
    FFT.compute(FFTDirection::Forward);
    FFT.complexToMagnitude();

    const double bin_hz = (double)MIC_SAMPLE_HZ / 512.0;
    for (int b = 0; b < 6; b++) {
      int bin_lo = (int)(band_edges[b]   / bin_hz);
      int bin_hi = (int)(band_edges[b+1] / bin_hz);
      double sum = 0;
      for (int k = bin_lo; k < bin_hi && k < 256; k++) sum += fft_real[k];
      mels[b] = (float)log10(sum + 1.0);
    }

    for (int b = 0; b < 6; b++) {
      g_mel_energies[b] = 0.7f * g_mel_energies[b] + 0.3f * mels[b];
    }
    g_mel_ready = true;
  }
}
