// Per-second aggregator: condense IMU/mic/thermal into one feature vector.

#include "features.h"
#include "config.h"
#include "imu_task.h"
#include "mic_task.h"
#include "thermal_task.h"
#include "mqtt_publisher.h"
#include <arduinoFFT.h>
#include <math.h>

static double *fft_real = nullptr;
static double *fft_imag = nullptr;
static ArduinoFFT<double> *fft_engine = nullptr;
static uint32_t seq_counter = 0;

void features_init() {
  fft_real = (double*)ps_malloc(IMU_FFT_SIZE * sizeof(double));
  fft_imag = (double*)ps_malloc(IMU_FFT_SIZE * sizeof(double));
  if (!fft_real || !fft_imag) {
    log_w("[FEATURES] PSRAM unavailable; allocating FFT buffers in internal RAM");
    if (fft_real) free(fft_real);
    if (fft_imag) free(fft_imag);
    fft_real = (double*)heap_caps_malloc(IMU_FFT_SIZE * sizeof(double), MALLOC_CAP_8BIT);
    fft_imag = (double*)heap_caps_malloc(IMU_FFT_SIZE * sizeof(double), MALLOC_CAP_8BIT);
    if (!fft_real || !fft_imag) {
      log_e("[FEATURES] FATAL: cannot allocate FFT buffers anywhere");
      return;
    }
  }
  fft_engine = new ArduinoFFT<double>(fft_real, fft_imag, IMU_FFT_SIZE, (double)IMU_SAMPLE_HZ);
  log_i("[FEATURES] FFT engine ready");
}

static void compute_imu_features(FeatureVector &f) {
  if (!fft_engine) {
    f.rms_g = 0; f.kurtosis = 0; f.crest_factor = 0; f.peak_hz = 0; f.peak_mag = 0;
    f.quality = 0;
    return;
  }
  float window[IMU_FFT_SIZE];
  if (!imu_snapshot(window, IMU_FFT_SIZE)) {
    f.quality = 0;
    return;
  }

  double mean = 0, m2 = 0, m4 = 0;
  float peak_abs = 0;
  for (int i = 0; i < IMU_FFT_SIZE; i++) mean += window[i];
  mean /= IMU_FFT_SIZE;
  for (int i = 0; i < IMU_FFT_SIZE; i++) {
    double d = window[i] - mean;
    m2 += d*d;
    m4 += d*d*d*d;
    if (fabsf(window[i]) > peak_abs) peak_abs = fabsf(window[i]);
  }
  m2 /= IMU_FFT_SIZE;
  m4 /= IMU_FFT_SIZE;
  double rms = sqrt(m2);

  f.rms_g        = (float)rms;
  f.kurtosis     = (float)(m2 > 1e-9 ? m4 / (m2*m2) : 0.0);
  f.crest_factor = (rms > 1e-6f) ? (peak_abs / (float)rms) : 0.0f;

  for (int i = 0; i < IMU_FFT_SIZE; i++) {
    fft_real[i] = window[i] - mean;
    fft_imag[i] = 0.0;
  }
  fft_engine->windowing(FFTWindow::Hamming, FFTDirection::Forward);
  fft_engine->compute(FFTDirection::Forward);
  fft_engine->complexToMagnitude();

  const double bin_hz = (double)IMU_SAMPLE_HZ / IMU_FFT_SIZE;
  int skip = (int)(5.0 / bin_hz);
  double max_mag = 0;
  int max_bin = skip;
  for (int k = skip; k < IMU_FFT_BINS; k++) {
    if (fft_real[k] > max_mag) {
      max_mag = fft_real[k];
      max_bin = k;
    }
  }
  f.peak_hz  = (float)(max_bin * bin_hz);
  f.peak_mag = (float)(max_mag / IMU_FFT_SIZE);
}

static void compute_mic_features(FeatureVector &f) {
  if (!g_mel_ready) {
    for (int i = 0; i < 6; i++) f.mel[i] = 0;
    return;
  }
  for (int i = 0; i < 6; i++) f.mel[i] = g_mel_energies[i];
}

static void compute_thermal_features(FeatureVector &f) {
  if (g_thermal_ready) {
    f.therm_min_c  = g_thermal_min_c;
    f.therm_mean_c = g_thermal_mean_c;
    f.therm_max_c  = g_thermal_max_c;
  } else {
    f.therm_min_c = f.therm_mean_c = f.therm_max_c = 0;
  }
}

static uint8_t assess_quality(const FeatureVector &f) {
  uint8_t q = 100;
  if (f.rms_g > BOUND_RMS_G_MAX || f.rms_g < 0)  q -= 40;
  if (f.peak_hz < BOUND_PEAK_HZ_MIN || f.peak_hz > BOUND_PEAK_HZ_MAX) q -= 20;
  if (f.therm_max_c < BOUND_TEMP_C_MIN || f.therm_max_c > BOUND_TEMP_C_MAX) q -= 20;
  if (!g_thermal_ready) q -= 10;
  if (!g_mel_ready)     q -= 10;
  return q;
}

void features_task(void *param) {
  TickType_t last_wake = xTaskGetTickCount();
  const TickType_t period = pdMS_TO_TICKS(1000 / FEATURE_PUBLISH_HZ);

  for (;;) {
    vTaskDelayUntil(&last_wake, period);

    FeatureVector f = {0};
    f.seq     = ++seq_counter;
    f.unix_ms = (uint32_t)(time(nullptr));

    compute_imu_features(f);
    compute_mic_features(f);
    compute_thermal_features(f);
    f.quality = assess_quality(f);

    mqtt_enqueue_feature(f);
  }
}
