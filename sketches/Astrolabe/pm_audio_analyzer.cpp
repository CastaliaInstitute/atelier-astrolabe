#include "pm_audio_analyzer.h"

#include <math.h>
#include <string.h>

#include "pm_audio_route.h"
#include "pm_fft.h"
#include "pm_mic.h"

static size_t s_in_fill[PM_AUDIO_ANALYZER_IN_CHANNELS];
static size_t s_out_fill = 0;

static float s_in_ch[PM_AUDIO_ANALYZER_IN_CHANNELS][PM_AUDIO_ANALYZER_BANDS];
static float s_out_disp[PM_AUDIO_ANALYZER_BANDS];
static float s_wave[PM_AUDIO_WAVE_POINTS];
static float s_spec_hist[PM_AUDIO_SPEC_HISTORY][PM_AUDIO_ANALYZER_BANDS];
static int s_spec_hist_count = 0;
static float s_level = 0.f;

static void push_spec_history(const float *bands) {
  if (s_spec_hist_count < PM_AUDIO_SPEC_HISTORY) {
    memcpy(s_spec_hist[s_spec_hist_count], bands, sizeof(s_spec_hist[0]));
    s_spec_hist_count++;
    return;
  }
  memmove(s_spec_hist[0], s_spec_hist[1],
          static_cast<size_t>(PM_AUDIO_SPEC_HISTORY - 1) * sizeof(s_spec_hist[0]));
  memcpy(s_spec_hist[PM_AUDIO_SPEC_HISTORY - 1], bands, sizeof(s_spec_hist[0]));
}

static void capture_waveform(const int16_t *block) {
  if (!block) {
    return;
  }
  const int step = PM_FFT_N / PM_AUDIO_WAVE_POINTS;
  if (step < 1) {
    return;
  }
  float peak = 1.f;
  for (int i = 0; i < PM_AUDIO_WAVE_POINTS; ++i) {
    const float v = static_cast<float>(block[i * step]) / 32768.f;
    const float a = fabsf(v);
    if (a > peak) {
      peak = a;
    }
    s_wave[i] = v;
  }
  const float inv = peak > 0.001f ? 1.f / peak : 1.f;
  for (int i = 0; i < PM_AUDIO_WAVE_POINTS; ++i) {
    s_wave[i] *= inv;
    if (s_wave[i] > 1.f) {
      s_wave[i] = 1.f;
    } else if (s_wave[i] < -1.f) {
      s_wave[i] = -1.f;
    }
  }
  float rms = 0.f;
  for (int i = 0; i < PM_AUDIO_WAVE_POINTS; ++i) {
    rms += s_wave[i] * s_wave[i];
  }
  rms = sqrtf(rms / static_cast<float>(PM_AUDIO_WAVE_POINTS));
  s_level = s_level * 0.6f + rms * 0.4f;
  if (s_level > 1.f) {
    s_level = 1.f;
  }
}

static void update_mix_history(int channel) {
  if (channel != 0) {
    return;
  }
  push_spec_history(s_in_ch[0]);
}

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

static void process_block_in(const int16_t *block, int channel) {
  if (channel < 0 || channel >= PM_AUDIO_ANALYZER_IN_CHANNELS) {
    return;
  }
  if (channel == 0) {
    capture_waveform(block);
  }
  static float mag[PM_FFT_BINS];
  pm_fft_compute_magnitude(block, mag, PM_FFT_BINS);
  mag_to_bands(mag, PM_FFT_BINS, 1, PM_FFT_BINS, s_in_ch[channel]);
  update_mix_history(channel);
}

static void process_block_out(const int16_t *block) {
  capture_waveform(block);
  static float mag[PM_FFT_BINS];
  pm_fft_compute_magnitude(block, mag, PM_FFT_BINS);
  mag_to_bands(mag, PM_FFT_BINS, 1, PM_FFT_BINS, s_out_disp);
}

void pm_audio_analyzer_reset(void) {
  for (int c = 0; c < PM_AUDIO_ANALYZER_IN_CHANNELS; ++c) {
    s_in_fill[c] = 0;
    memset(s_in_ch[c], 0, sizeof(s_in_ch[c]));
  }
  s_out_fill = 0;
  memset(s_out_disp, 0, sizeof(s_out_disp));
  memset(s_wave, 0, sizeof(s_wave));
  memset(s_spec_hist, 0, sizeof(s_spec_hist));
  s_spec_hist_count = 0;
  s_level = 0.f;
  pm_fft_init();
}

void pm_audio_analyzer_feed_in_channel(int channel, const int16_t *pcm, size_t num_samples) {
  if (!pcm || num_samples == 0 || channel < 0 || channel >= PM_AUDIO_ANALYZER_IN_CHANNELS) {
    return;
  }
  static int16_t block[PM_FFT_N];
  for (size_t i = 0; i < num_samples; ++i) {
    block[s_in_fill[channel]] = pcm[i];
    s_in_fill[channel]++;
    if (s_in_fill[channel] >= PM_FFT_N) {
      process_block_in(block, channel);
      s_in_fill[channel] = 0;
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
  memcpy(bands, s_in_ch[0], n * sizeof(float));
}

void pm_audio_analyzer_get_in_high(float *bands, size_t count) {
  const size_t n = count < PM_AUDIO_ANALYZER_BANDS ? count : PM_AUDIO_ANALYZER_BANDS;
  memcpy(bands, s_in_ch[1], n * sizeof(float));
}

void pm_audio_analyzer_get_out(float *bands, size_t count) {
  const size_t n = count < PM_AUDIO_ANALYZER_BANDS ? count : PM_AUDIO_ANALYZER_BANDS;
  memcpy(bands, s_out_disp, n * sizeof(float));
}

void pm_audio_analyzer_get_mix(float *bands, size_t count) {
  const size_t n = count < PM_AUDIO_ANALYZER_BANDS ? count : PM_AUDIO_ANALYZER_BANDS;
  for (size_t b = 0; b < n; ++b) {
    float v = s_in_ch[0][b];
    if (s_in_ch[1][b] > v) {
      v = s_in_ch[1][b];
    }
    if (s_out_disp[b] > v) {
      v = s_out_disp[b];
    }
    bands[b] = v;
  }
}

void pm_audio_analyzer_get_waveform(float *samples, size_t count) {
  const size_t n = count < PM_AUDIO_WAVE_POINTS ? count : PM_AUDIO_WAVE_POINTS;
  memcpy(samples, s_wave, n * sizeof(float));
}

void pm_audio_analyzer_get_spec_history(float *rows, int count, int bands) {
  if (!rows || count <= 0 || bands <= 0) {
    return;
  }
  const int nrows = count > PM_AUDIO_SPEC_HISTORY ? PM_AUDIO_SPEC_HISTORY : count;
  const int nb = bands > PM_AUDIO_ANALYZER_BANDS ? PM_AUDIO_ANALYZER_BANDS : bands;
  const int pad = nrows > s_spec_hist_count ? nrows - s_spec_hist_count : 0;
  for (int r = 0; r < nrows; ++r) {
    float *dst = rows + r * bands;
    const int src = r - pad;
    if (src < 0) {
      memset(dst, 0, static_cast<size_t>(nb) * sizeof(float));
    } else {
      memcpy(dst, s_spec_hist[src], static_cast<size_t>(nb) * sizeof(float));
    }
  }
}

float pm_audio_analyzer_get_level(void) { return s_level; }

#ifndef ASTROLABE_QEMU

bool pm_audio_analyzer_mic_begin(void) {
  if (pm_audio_route_input_usb()) {
    return true;
  }
  return pm_mic_begin();
}

void pm_audio_analyzer_mic_end(void) {
  if (!pm_audio_route_input_usb()) {
    pm_mic_stop();
  }
}

void pm_audio_analyzer_tick(void) {
  if (pm_audio_route_input_usb()) {
    for (int b = 0; b < PM_AUDIO_ANALYZER_BANDS; ++b) {
      s_out_disp[b] *= 0.92f;
    }
    return;
  }
  static int16_t raw[512 * 2];
  static int16_t mono[512];
  const size_t ns = pm_mic_frame_samples();
  const int nch = pm_mic_i2s_channels();
  if (ns > sizeof(mono) / sizeof(mono[0]) || ns * static_cast<size_t>(nch) > sizeof(raw) / sizeof(raw[0])) {
    return;
  }
  size_t br = 0;
  if (pm_mic_read_frame(raw, ns, &br)) {
    const int feed_ch = nch < PM_AUDIO_ANALYZER_IN_CHANNELS ? nch : PM_AUDIO_ANALYZER_IN_CHANNELS;
    for (int c = 0; c < feed_ch; ++c) {
      pm_mic_pick_channel(raw, ns, c, mono);
      pm_audio_analyzer_feed_in_channel(c, mono, ns);
    }
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
  pm_audio_analyzer_feed_in_channel(0, fake, PM_FFT_N);
  for (int i = 0; i < PM_FFT_N; ++i) {
    const float t = static_cast<float>(s_phase + static_cast<uint32_t>(i)) * 0.11f;
    fake[i] = static_cast<int16_t>(6500.f * sinf(t * 1.7f));
  }
  pm_audio_analyzer_feed_in_channel(1, fake, PM_FFT_N);
  for (int i = 0; i < PM_FFT_N; ++i) {
    const float t = static_cast<float>(s_phase + static_cast<uint32_t>(i)) * 0.19f;
    fake[i] = static_cast<int16_t>(7000.f * sinf(t));
  }
  pm_audio_analyzer_feed_out(fake, PM_FFT_N, 1);
}

#endif
