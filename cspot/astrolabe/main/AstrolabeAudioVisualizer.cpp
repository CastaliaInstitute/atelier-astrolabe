#include "AstrolabeAudioVisualizer.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "pm_fft.h"

static SemaphoreHandle_t s_visMutex = nullptr;
static int16_t s_block[PM_FFT_N];
static size_t s_fill = 0;
static uint8_t s_decim = 0;
static float s_bands[ASTROLABE_AUDIO_VIS_BANDS];
static float s_wave[ASTROLABE_AUDIO_VIS_WAVE_POINTS];
static float s_level = 0.f;
static uint32_t s_blocks = 0;

static SemaphoreHandle_t visualizerMutex() {
  if (!s_visMutex) {
    s_visMutex = xSemaphoreCreateMutex();
  }
  return s_visMutex;
}

static bool lockVis(TickType_t wait = pdMS_TO_TICKS(2)) {
  SemaphoreHandle_t mutex = visualizerMutex();
  return mutex && xSemaphoreTake(mutex, wait) == pdTRUE;
}

static void unlockVis() {
  if (s_visMutex) {
    xSemaphoreGive(s_visMutex);
  }
}

static void captureWaveform(const int16_t *block) {
  const int step = PM_FFT_N / ASTROLABE_AUDIO_VIS_WAVE_POINTS;
  float peak = 0.f;
  float rawRms = 0.f;
  for (int i = 0; i < ASTROLABE_AUDIO_VIS_WAVE_POINTS; ++i) {
    const float v = static_cast<float>(block[i * step]) / 32768.f;
    peak = std::max(peak, fabsf(v));
    rawRms += v * v;
    s_wave[i] = v;
  }
  rawRms = sqrtf(rawRms / static_cast<float>(ASTROLABE_AUDIO_VIS_WAVE_POINTS));
  const float inv = peak > 0.0008f ? 1.f / peak : 1.f;
  for (float &v : s_wave) {
    v = std::max(-1.f, std::min(1.f, v * inv));
  }
  float level = (rawRms - 0.00025f) * 140.f;
  level = std::max(0.f, std::min(1.f, level));
  s_level = std::max(0.f, std::min(1.f, s_level * 0.55f + level * 0.45f));
}

static void magToBands(const float *mag, int magBins) {
  float peak = 0.f;
  for (int b = 0; b < ASTROLABE_AUDIO_VIS_BANDS; ++b) {
    const int k0 = 1 + (b * (magBins - 1)) / ASTROLABE_AUDIO_VIS_BANDS;
    const int k1 = 1 + ((b + 1) * (magBins - 1)) / ASTROLABE_AUDIO_VIS_BANDS;
    float sum = 0.f;
    int count = 0;
    for (int k = k0; k < k1 && k < magBins; ++k) {
      sum += mag[k];
      ++count;
    }
    const float avg = count > 0 ? sum / static_cast<float>(count) : 0.f;
    float v = log10f(1.f + avg * 0.08f);
    v = std::max(0.f, std::min(1.f, v));
    s_bands[b] = s_bands[b] * 0.55f + v * 0.45f;
    peak = std::max(peak, s_bands[b]);
  }
  if (peak > 0.0001f) {
    const float inv = 1.f / peak;
    for (float &band : s_bands) {
      band = std::max(0.f, std::min(1.f, band * inv));
    }
  }
}

static void processBlock(const int16_t *block) {
  float mag[PM_FFT_BINS];
  pm_fft_compute_magnitude(block, mag, PM_FFT_BINS);
  captureWaveform(block);
  magToBands(mag, PM_FFT_BINS);
  ++s_blocks;
}

void astrolabe_audio_visualizer_reset() {
  if (!lockVis(pdMS_TO_TICKS(20))) {
    return;
  }
  s_fill = 0;
  s_decim = 0;
  s_level = 0.f;
  s_blocks = 0;
  memset(s_block, 0, sizeof(s_block));
  memset(s_bands, 0, sizeof(s_bands));
  memset(s_wave, 0, sizeof(s_wave));
  unlockVis();
  pm_fft_init();
}

void astrolabe_audio_visualizer_feed_output_pcm(const int16_t *pcm, size_t num_s16, int channels) {
  if (!pcm || num_s16 == 0) {
    return;
  }
  const int ch = channels > 0 ? channels : 1;
  const size_t frames = num_s16 / static_cast<size_t>(ch);
  if (frames == 0 || !lockVis()) {
    return;
  }
  for (size_t f = 0; f < frames; ++f) {
    if (++s_decim < 4) {
      continue;
    }
    s_decim = 0;
    s_block[s_fill++] = pcm[f * static_cast<size_t>(ch)];
    if (s_fill >= PM_FFT_N) {
      int16_t block[PM_FFT_N];
      memcpy(block, s_block, sizeof(block));
      s_fill = 0;
      processBlock(block);
    }
  }
  unlockVis();
}

void astrolabe_audio_visualizer_get(float *bands, size_t band_count, float *wave, size_t wave_count, float *level) {
  if (!lockVis()) {
    if (bands) {
      memset(bands, 0, band_count * sizeof(float));
    }
    if (wave) {
      memset(wave, 0, wave_count * sizeof(float));
    }
    if (level) {
      *level = 0.f;
    }
    return;
  }
  if (bands) {
    const size_t n = std::min(band_count, static_cast<size_t>(ASTROLABE_AUDIO_VIS_BANDS));
    memcpy(bands, s_bands, n * sizeof(float));
    if (band_count > n) {
      memset(bands + n, 0, (band_count - n) * sizeof(float));
    }
  }
  if (wave) {
    const size_t n = std::min(wave_count, static_cast<size_t>(ASTROLABE_AUDIO_VIS_WAVE_POINTS));
    memcpy(wave, s_wave, n * sizeof(float));
    if (wave_count > n) {
      memset(wave + n, 0, (wave_count - n) * sizeof(float));
    }
  }
  if (level) {
    *level = s_level;
  }
  unlockVis();
}

uint32_t astrolabe_audio_visualizer_blocks() {
  if (!lockVis()) {
    return 0;
  }
  const uint32_t blocks = s_blocks;
  unlockVis();
  return blocks;
}
