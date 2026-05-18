#include "pm_audio_analyzer.h"

#include <math.h>
#include <string.h>

#include "pm_fft.h"
#include "pm_mic.h"

static size_t s_in_fill = 0;
static size_t s_out_fill = 0;

static float s_in_low[PM_AUDIO_ANALYZER_BANDS];
static float s_in_high[PM_AUDIO_ANALYZER_BANDS];
static float s_out_disp[PM_AUDIO_ANALYZER_BANDS];

static void mag_to_bands(const float *mag, int mag_bins, int k_start, int k_end, float *disp) {
  const int span = k_end - k_start;
  if (span <= 0) {
    return;
  }
  float peak = 1.f;
  for (int b = 0; b < PM_AUDIO_ANALYZER_BANDS; ++b) {
    const int k0 = k_start + (b * span) / PM_AUDIO_ANALYZER_BANDS;
    const int k1 = k_start + ((b + 1) * span) / PM_AUDIO_ANALYZER_BANDS;
    float sum = 0.f;
    int cnt = 0;
    for (int k = k0; k < k1 && k < mag_bins; ++k) {
      sum += mag[k];
      ++cnt;
    }
    const float avg = cnt > 0 ? sum / static_cast<float>(cnt) : 0.f;
    float v = log10f(1.f + avg * 0.002f);
    if (v > 1.f) {
      v = 1.f;
    }
    disp[b] = disp[b] * 0.55f + v * 0.45f;
    if (disp[b] > peak) {
      peak = disp[b];
    }
  }
  if (peak > 0.01f) {
    const float inv = 1.f / peak;
    for (int b = 0; b < PM_AUDIO_ANALYZER_BANDS; ++b) {
      disp[b] *= inv;
      if (disp[b] > 1.f) {
        disp[b] = 1.f;
      }
    }
  }
}

static void process_block_in(const int16_t *block) {
  static float mag[PM_FFT_BINS];
  pm_fft_compute_magnitude(block, mag, PM_FFT_BINS);
  const int mid = PM_FFT_BINS / 2;
  mag_to_bands(mag, PM_FFT_BINS, 1, mid, s_in_low);
  mag_to_bands(mag, PM_FFT_BINS, mid, PM_FFT_BINS, s_in_high);
}

static void process_block_out(const int16_t *block) {
  static float mag[PM_FFT_BINS];
  pm_fft_compute_magnitude(block, mag, PM_FFT_BINS);
  mag_to_bands(mag, PM_FFT_BINS, 1, PM_FFT_BINS, s_out_disp);
}

void pm_audio_analyzer_reset(void) {
  s_in_fill = 0;
  s_out_fill = 0;
  memset(s_in_low, 0, sizeof(s_in_low));
  memset(s_in_high, 0, sizeof(s_in_high));
  memset(s_out_disp, 0, sizeof(s_out_disp));
  pm_fft_init();
}

void pm_audio_analyzer_feed_in(const int16_t *pcm, size_t num_samples) {
  if (!pcm || num_samples == 0) {
    return;
  }
  static int16_t block[PM_FFT_N];
  for (size_t i = 0; i < num_samples; ++i) {
    block[s_in_fill] = pcm[i];
    s_in_fill++;
    if (s_in_fill >= PM_FFT_N) {
      process_block_in(block);
      s_in_fill = 0;
    }
  }
}

void pm_audio_analyzer_feed_out(const int16_t *pcm, size_t num_s16, int channels) {
  if (!pcm || num_s16 == 0) {
    return;
  }
  const int ch = channels > 0 ? channels : 1;
  static int16_t block[PM_FFT_N];
  const size_t frames = num_s16 / static_cast<size_t>(ch);
  for (size_t f = 0; f < frames; ++f) {
    const int16_t s = pcm[f * static_cast<size_t>(ch)];
    block[s_out_fill] = s;
    s_out_fill++;
    if (s_out_fill >= PM_FFT_N) {
      process_block_out(block);
      s_out_fill = 0;
    }
  }
}

void pm_audio_analyzer_get_in_low(float *bands, size_t count) {
  const size_t n = count < PM_AUDIO_ANALYZER_BANDS ? count : PM_AUDIO_ANALYZER_BANDS;
  memcpy(bands, s_in_low, n * sizeof(float));
}

void pm_audio_analyzer_get_in_high(float *bands, size_t count) {
  const size_t n = count < PM_AUDIO_ANALYZER_BANDS ? count : PM_AUDIO_ANALYZER_BANDS;
  memcpy(bands, s_in_high, n * sizeof(float));
}

void pm_audio_analyzer_get_out(float *bands, size_t count) {
  const size_t n = count < PM_AUDIO_ANALYZER_BANDS ? count : PM_AUDIO_ANALYZER_BANDS;
  memcpy(bands, s_out_disp, n * sizeof(float));
}

#ifndef ASTROLABE_QEMU

bool pm_audio_analyzer_mic_begin(void) {
  return pm_mic_begin();
}

void pm_audio_analyzer_mic_end(void) {
  pm_mic_stop();
}

void pm_audio_analyzer_tick(void) {
  static int16_t frame[512];
  const size_t ns = pm_mic_frame_samples();
  if (ns > sizeof(frame) / sizeof(frame[0])) {
    return;
  }
  size_t br = 0;
  if (pm_mic_read_frame(frame, ns, &br)) {
    pm_audio_analyzer_feed_in(frame, ns);
  }
  for (int b = 0; b < PM_AUDIO_ANALYZER_BANDS; ++b) {
    s_out_disp[b] *= 0.92f;
  }
}

#else

void pm_audio_analyzer_tick(void) {
  static uint32_t s_phase = 0;
  s_phase += 17;
  static int16_t fake[PM_FFT_N];
  for (int i = 0; i < PM_FFT_N; ++i) {
    const float t = static_cast<float>(s_phase + static_cast<uint32_t>(i)) * 0.05f;
    fake[i] = static_cast<int16_t>(9000.f * sinf(t));
  }
  pm_audio_analyzer_feed_in(fake, PM_FFT_N);
  for (int i = 0; i < PM_FFT_N; ++i) {
    const float t = static_cast<float>(s_phase + static_cast<uint32_t>(i)) * 0.19f;
    fake[i] = static_cast<int16_t>(7000.f * sinf(t));
  }
  pm_audio_analyzer_feed_out(fake, PM_FFT_N, 1);
}

#endif
