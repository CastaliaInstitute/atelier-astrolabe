#include "pm_audio_analyzer.h"

#include <math.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "pm_audio_route.h"
#include "pm_fft.h"
#include "pm_mic.h"
#include "pm_resource.h"

static SemaphoreHandle_t s_analyzer_mux = nullptr;
static size_t s_in_fill[PM_AUDIO_ANALYZER_IN_CHANNELS];
static size_t s_out_fill = 0;

static int16_t s_in_block[PM_AUDIO_ANALYZER_IN_CHANNELS][PM_FFT_N];
static int16_t s_out_block[PM_FFT_N];
static int16_t s_echo_ref[PM_FFT_N];
static constexpr size_t PM_AEC_TAPS = 128;
static constexpr size_t PM_AEC_REF_LEN = 2048;
static constexpr size_t PM_AEC_REF_MASK = PM_AEC_REF_LEN - 1;
static constexpr size_t PM_AEC_DELAY_SAMPLES = 480;
static int16_t s_aec_ref[PM_AEC_REF_LEN];
static size_t s_aec_ref_pos = 0;
static uint32_t s_aec_resample_acc = 0;
static float s_aec_w[PM_AUDIO_ANALYZER_IN_CHANNELS][PM_AEC_TAPS];
static float s_aec_err_rms[PM_AUDIO_ANALYZER_IN_CHANNELS];
static float s_aec_ref_rms = 0.f;
static uint32_t s_aec_adapt_blocks[PM_AUDIO_ANALYZER_IN_CHANNELS];
static float s_in_ch[PM_AUDIO_ANALYZER_IN_CHANNELS][PM_AUDIO_ANALYZER_BANDS];
static float s_out_disp[PM_AUDIO_ANALYZER_BANDS];
static float s_wave[PM_AUDIO_WAVE_POINTS];
static float s_spec_hist[PM_AUDIO_SPEC_HISTORY][PM_AUDIO_ANALYZER_BANDS];
static int s_spec_hist_count = 0;
static float s_level = 0.f;
static float s_in_peak[PM_AUDIO_ANALYZER_IN_CHANNELS];
static float s_out_peak = 0.f;
static uint32_t s_in_blocks[PM_AUDIO_ANALYZER_IN_CHANNELS];
static uint32_t s_out_blocks = 0;
static PmAudioPitch s_pitch = {};

static SemaphoreHandle_t analyzer_mutex(void) {
  if (!s_analyzer_mux) {
    s_analyzer_mux = xSemaphoreCreateMutex();
  }
  return s_analyzer_mux;
}

static bool analyzer_lock(TickType_t wait = pdMS_TO_TICKS(20)) {
  SemaphoreHandle_t mux = analyzer_mutex();
  return mux && xSemaphoreTake(mux, wait) == pdTRUE;
}

static void analyzer_unlock(void) {
  if (s_analyzer_mux) {
    xSemaphoreGive(s_analyzer_mux);
  }
}

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
  float peak = 0.f;
  float raw_rms = 0.f;
  for (int i = 0; i < PM_AUDIO_WAVE_POINTS; ++i) {
    const float v = static_cast<float>(block[i * step]) / 32768.f;
    const float a = fabsf(v);
    if (a > peak) {
      peak = a;
    }
    raw_rms += v * v;
    s_wave[i] = v;
  }
  raw_rms = sqrtf(raw_rms / static_cast<float>(PM_AUDIO_WAVE_POINTS));
  const float inv = peak > 0.0008f ? 1.f / peak : 1.f;
  for (int i = 0; i < PM_AUDIO_WAVE_POINTS; ++i) {
    s_wave[i] *= inv;
    if (s_wave[i] > 1.f) {
      s_wave[i] = 1.f;
    } else if (s_wave[i] < -1.f) {
      s_wave[i] = -1.f;
    }
  }
  float level = (raw_rms - 0.00025f) * 140.f;
  if (level < 0.f) {
    level = 0.f;
  } else if (level > 1.f) {
    level = 1.f;
  }
  s_level = s_level * 0.55f + level * 0.45f;
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

static float pcm_peak(const int16_t *block) {
  if (!block) {
    return 0.f;
  }
  int32_t peak = 0;
  for (int i = 0; i < PM_FFT_N; ++i) {
    const int32_t v = block[i] < 0 ? -static_cast<int32_t>(block[i]) : static_cast<int32_t>(block[i]);
    if (v > peak) {
      peak = v;
    }
  }
  return static_cast<float>(peak) / 32768.f;
}

static float pcm_peak_samples(const int16_t *samples, size_t count) {
  if (!samples || count == 0) {
    return 0.f;
  }
  int32_t peak = 0;
  for (size_t i = 0; i < count; ++i) {
    const int32_t v = samples[i] < 0 ? -static_cast<int32_t>(samples[i]) : static_cast<int32_t>(samples[i]);
    if (v > peak) {
      peak = v;
    }
  }
  return static_cast<float>(peak) / 32768.f;
}

static void note_name_from_midi(int midi, char *out, size_t out_len) {
  if (!out || out_len == 0) {
    return;
  }
  static const char *kNames[12] = {"C",  "C#", "D",  "D#", "E",  "F",
                                   "F#", "G",  "G#", "A",  "A#", "B"};
  int pc = midi % 12;
  if (pc < 0) {
    pc += 12;
  }
  snprintf(out, out_len, "%s", kNames[pc]);
}

static void update_pitch_from_block(const int16_t *block) {
  PmAudioPitch next = {};
  if (!block) {
    s_pitch = next;
    return;
  }
  constexpr float kSampleHz = 16000.f;
  constexpr int kLagMin = 13;   // ~1230 Hz
  constexpr int kLagMax = 228;  // ~70 Hz
  float mean = 0.f;
  for (int i = 0; i < PM_FFT_N; ++i) {
    mean += static_cast<float>(block[i]);
  }
  mean /= static_cast<float>(PM_FFT_N);

  float total_energy = 0.f;
  for (int i = 0; i < PM_FFT_N; ++i) {
    const float v = static_cast<float>(block[i]) - mean;
    total_energy += v * v;
  }
  const float rms = sqrtf(total_energy / static_cast<float>(PM_FFT_N)) / 32768.f;
  if (rms < 0.004f) {
    s_pitch.valid = false;
    s_pitch.level *= 0.78f;
    return;
  }

  float corr[kLagMax + 1];
  memset(corr, 0, sizeof(corr));
  int best_lag = 0;
  float best = 0.f;
  for (int lag = kLagMin; lag <= kLagMax; ++lag) {
    float sum = 0.f;
    float e0 = 0.f;
    float e1 = 0.f;
    const int n = PM_FFT_N - lag;
    for (int i = 0; i < n; ++i) {
      const float a = static_cast<float>(block[i]) - mean;
      const float b = static_cast<float>(block[i + lag]) - mean;
      sum += a * b;
      e0 += a * a;
      e1 += b * b;
    }
    const float norm = sqrtf(e0 * e1);
    const float c = norm > 1.f ? sum / norm : 0.f;
    corr[lag] = c;
    if (c > best) {
      best = c;
      best_lag = lag;
    }
  }

  if (best_lag <= 0 || best < 0.48f) {
    s_pitch.valid = false;
    s_pitch.confidence = s_pitch.confidence * 0.65f;
    s_pitch.level = s_pitch.level * 0.7f + rms * 16.f * 0.3f;
    return;
  }

  float lag_f = static_cast<float>(best_lag);
  if (best_lag > kLagMin && best_lag < kLagMax) {
    const float ym1 = corr[best_lag - 1];
    const float y0 = corr[best_lag];
    const float yp1 = corr[best_lag + 1];
    const float denom = ym1 - 2.f * y0 + yp1;
    if (fabsf(denom) > 0.0001f) {
      const float delta = 0.5f * (ym1 - yp1) / denom;
      if (delta > -0.75f && delta < 0.75f) {
        lag_f += delta;
      }
    }
  }

  float hz = kSampleHz / lag_f;
  while (hz < 70.f) {
    hz *= 2.f;
  }
  while (hz > 1250.f) {
    hz *= 0.5f;
  }
  const float midi_f = 69.f + 12.f * log2f(hz / 440.f);
  const int midi = static_cast<int>(lrintf(midi_f));
  if (midi < 24 || midi > 96) {
    s_pitch.valid = false;
    return;
  }
  const float target_hz = 440.f * powf(2.f, (static_cast<float>(midi) - 69.f) / 12.f);
  int cents = static_cast<int>(lrintf(1200.f * log2f(hz / target_hz)));
  if (cents < -99) {
    cents = -99;
  } else if (cents > 99) {
    cents = 99;
  }

  next.valid = true;
  next.hz = s_pitch.valid ? (s_pitch.hz * 0.72f + hz * 0.28f) : hz;
  next.midi = midi;
  next.cents = cents;
  next.confidence = s_pitch.confidence * 0.55f + best * 0.45f;
  next.level = s_pitch.level * 0.65f + (rms * 16.f) * 0.35f;
  if (next.level > 1.f) {
    next.level = 1.f;
  }
  note_name_from_midi(midi, next.note, sizeof(next.note));
  s_pitch = next;
}

static void mag_to_bands(const float *mag, int mag_bins, int k_start, int k_end, float *disp) {
  const int span = k_end - k_start;
  if (span <= 0) {
    return;
  }
  float peak = 0.f;
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
    float v = log10f(1.f + avg * 0.08f);
    if (v > 1.f) {
      v = 1.f;
    }
    disp[b] = disp[b] * 0.55f + v * 0.45f;
    if (disp[b] > peak) {
      peak = disp[b];
    }
  }
  if (peak > 0.0001f) {
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
  float mag[PM_FFT_BINS];
  pm_fft_compute_magnitude(block, mag, PM_FFT_BINS);
  if (channel == 0) {
    capture_waveform(block);
    update_pitch_from_block(block);
  }
  s_in_peak[channel] = s_in_peak[channel] * 0.7f + pcm_peak(block) * 0.3f;
  s_in_blocks[channel]++;
  mag_to_bands(mag, PM_FFT_BINS, 1, PM_FFT_BINS, s_in_ch[channel]);
  update_mix_history(channel);
}

static void process_block_out(const int16_t *block) {
  float mag[PM_FFT_BINS];
  pm_fft_compute_magnitude(block, mag, PM_FFT_BINS);
  capture_waveform(block);
  s_out_peak = s_out_peak * 0.7f + pcm_peak(block) * 0.3f;
  s_out_blocks++;
  memcpy(s_echo_ref, block, sizeof(s_echo_ref));
  mag_to_bands(mag, PM_FFT_BINS, 1, PM_FFT_BINS, s_out_disp);
}

static void aec_feed_ref_sample(int16_t sample) {
  s_aec_ref[s_aec_ref_pos] = sample;
  s_aec_ref_pos = (s_aec_ref_pos + 1u) & PM_AEC_REF_MASK;
}

static float aec_sample_at(size_t newest_pos, size_t sample_back) {
  return static_cast<float>(s_aec_ref[(newest_pos - sample_back) & PM_AEC_REF_MASK]) / 32768.f;
}

void pm_audio_analyzer_reset(void) {
  if (!analyzer_lock()) {
    return;
  }
  for (int c = 0; c < PM_AUDIO_ANALYZER_IN_CHANNELS; ++c) {
    s_in_fill[c] = 0;
    memset(s_in_block[c], 0, sizeof(s_in_block[c]));
    memset(s_in_ch[c], 0, sizeof(s_in_ch[c]));
  }
  s_out_fill = 0;
  memset(s_out_block, 0, sizeof(s_out_block));
  memset(s_echo_ref, 0, sizeof(s_echo_ref));
  memset(s_aec_ref, 0, sizeof(s_aec_ref));
  s_aec_ref_pos = 0;
  s_aec_resample_acc = 0;
  memset(s_aec_w, 0, sizeof(s_aec_w));
  memset(s_aec_err_rms, 0, sizeof(s_aec_err_rms));
  memset(s_aec_adapt_blocks, 0, sizeof(s_aec_adapt_blocks));
  s_aec_ref_rms = 0.f;
  memset(s_out_disp, 0, sizeof(s_out_disp));
  memset(s_wave, 0, sizeof(s_wave));
  memset(s_spec_hist, 0, sizeof(s_spec_hist));
  s_spec_hist_count = 0;
  s_level = 0.f;
  memset(s_in_peak, 0, sizeof(s_in_peak));
  s_out_peak = 0.f;
  memset(s_in_blocks, 0, sizeof(s_in_blocks));
  s_out_blocks = 0;
  memset(&s_pitch, 0, sizeof(s_pitch));
  analyzer_unlock();
  pm_fft_init();
}

void pm_audio_analyzer_cancel_echo_channel(int channel, int16_t *pcm, size_t num_samples) {
  if (!pcm || num_samples == 0 || channel < 0 || channel >= PM_AUDIO_ANALYZER_IN_CHANNELS) {
    return;
  }
  if (!analyzer_lock(pdMS_TO_TICKS(4))) {
    return;
  }
  if (s_out_peak < 0.01f || s_aec_ref_rms < 0.0015f) {
    analyzer_unlock();
    return;
  }

  float *w = s_aec_w[channel];
  const float mu = 0.055f;
  const float leak = 0.99998f;
  const float eps = 0.00035f;
  float err_acc = 0.f;
  float ref_acc = 0.f;
  float mic_acc = 0.f;
  size_t ref_base = (s_aec_ref_pos - num_samples - PM_AEC_DELAY_SAMPLES) & PM_AEC_REF_MASK;

  for (size_t i = 0; i < num_samples; ++i) {
    const float d = static_cast<float>(pcm[i]) / 32768.f;
    float y = 0.f;
    float norm = eps;
    const size_t newest = (ref_base + i) & PM_AEC_REF_MASK;
    for (size_t tap = 0; tap < PM_AEC_TAPS; ++tap) {
      const float x = aec_sample_at(newest, tap);
      y += w[tap] * x;
      norm += x * x;
    }

    float e = d - y;
    mic_acc += d * d;
    ref_acc += norm - eps;
    const float echo_power = y * y;
    const bool double_talk = (d * d) > (echo_power * 8.f + 0.0012f);
    const bool adapt = !double_talk && norm > 0.002f;
    if (adapt) {
      const float step = mu * e / norm;
      for (size_t tap = 0; tap < PM_AEC_TAPS; ++tap) {
        const float x = aec_sample_at(newest, tap);
        w[tap] = w[tap] * leak + step * x;
        if (w[tap] > 1.25f) {
          w[tap] = 1.25f;
        } else if (w[tap] < -1.25f) {
          w[tap] = -1.25f;
        }
      }
    }

    if (s_out_peak > 0.04f && fabsf(e) < fabsf(d)) {
      e *= 0.82f;
    }
    if (e > 0.999f) {
      e = 0.999f;
    } else if (e < -1.f) {
      e = -1.f;
    }
    pcm[i] = static_cast<int16_t>(lrintf(e * 32767.f));
    err_acc += e * e;
  }
  const float n = static_cast<float>(num_samples);
  s_aec_err_rms[channel] = s_aec_err_rms[channel] * 0.85f + sqrtf(err_acc / n) * 0.15f;
  if (ref_acc > 0.01f && mic_acc > 0.00001f) {
    s_aec_adapt_blocks[channel]++;
  }
  analyzer_unlock();
}

void pm_audio_analyzer_cancel_echo(int16_t *pcm, size_t num_samples) {
  pm_audio_analyzer_cancel_echo_channel(0, pcm, num_samples);
}

void pm_audio_analyzer_feed_in_channel(int channel, const int16_t *pcm, size_t num_samples) {
  if (!pcm || num_samples == 0 || channel < 0 || channel >= PM_AUDIO_ANALYZER_IN_CHANNELS) {
    return;
  }
  if (!analyzer_lock()) {
    return;
  }
  for (size_t i = 0; i < num_samples; ++i) {
    s_in_block[channel][s_in_fill[channel]] = pcm[i];
    s_in_fill[channel]++;
    if (s_in_fill[channel] >= PM_FFT_N) {
      int16_t block[PM_FFT_N];
      memcpy(block, s_in_block[channel], sizeof(block));
      s_in_fill[channel] = 0;
      process_block_in(block, channel);
    }
  }
  analyzer_unlock();
}

void pm_audio_analyzer_feed_out_rate(const int16_t *pcm, size_t num_s16, int channels, int sample_hz) {
  if (!pcm || num_s16 == 0) {
    return;
  }
  if (!analyzer_lock()) {
    return;
  }
  const int ch = channels > 0 ? channels : 1;
  const uint32_t src_hz = sample_hz > 0 ? static_cast<uint32_t>(sample_hz) : 16000u;
  const size_t frames = num_s16 / static_cast<size_t>(ch);
  size_t aec_frames = 0;
  for (size_t f = 0; f < frames; ++f) {
    const int16_t s = pcm[f * static_cast<size_t>(ch)];
    s_aec_resample_acc += 16000u;
    while (s_aec_resample_acc >= src_hz) {
      s_aec_resample_acc -= src_hz;
      aec_feed_ref_sample(s);
      ++aec_frames;
    }
    s_out_block[s_out_fill] = s;
    s_out_fill++;
    if (s_out_fill >= PM_FFT_N) {
      int16_t block[PM_FFT_N];
      memcpy(block, s_out_block, sizeof(block));
      s_out_fill = 0;
      process_block_out(block);
    }
  }
  if (aec_frames > 0) {
    float acc = 0.f;
    const size_t sample_count = aec_frames < 256u ? aec_frames : 256u;
    size_t newest = (s_aec_ref_pos - 1u) & PM_AEC_REF_MASK;
    for (size_t i = 0; i < sample_count; ++i) {
      const float v = aec_sample_at(newest, i);
      acc += v * v;
    }
    s_aec_ref_rms = s_aec_ref_rms * 0.8f + sqrtf(acc / static_cast<float>(sample_count)) * 0.2f;
  }
  analyzer_unlock();
}

void pm_audio_analyzer_feed_out(const int16_t *pcm, size_t num_s16, int channels) {
  pm_audio_analyzer_feed_out_rate(pcm, num_s16, channels, 16000);
}

void pm_audio_analyzer_get_in_low(float *bands, size_t count) {
  if (!bands) {
    return;
  }
  if (!analyzer_lock(pdMS_TO_TICKS(5))) {
    memset(bands, 0, count * sizeof(float));
    return;
  }
  const size_t n = count < PM_AUDIO_ANALYZER_BANDS ? count : PM_AUDIO_ANALYZER_BANDS;
  memcpy(bands, s_in_ch[0], n * sizeof(float));
  analyzer_unlock();
}

void pm_audio_analyzer_get_in_high(float *bands, size_t count) {
  if (!bands) {
    return;
  }
  if (!analyzer_lock(pdMS_TO_TICKS(5))) {
    memset(bands, 0, count * sizeof(float));
    return;
  }
  const size_t n = count < PM_AUDIO_ANALYZER_BANDS ? count : PM_AUDIO_ANALYZER_BANDS;
  memcpy(bands, s_in_ch[1], n * sizeof(float));
  analyzer_unlock();
}

void pm_audio_analyzer_get_out(float *bands, size_t count) {
  if (!bands) {
    return;
  }
  if (!analyzer_lock(pdMS_TO_TICKS(5))) {
    memset(bands, 0, count * sizeof(float));
    return;
  }
  const size_t n = count < PM_AUDIO_ANALYZER_BANDS ? count : PM_AUDIO_ANALYZER_BANDS;
  memcpy(bands, s_out_disp, n * sizeof(float));
  analyzer_unlock();
}

void pm_audio_analyzer_get_mix(float *bands, size_t count) {
  if (!bands) {
    return;
  }
  if (!analyzer_lock(pdMS_TO_TICKS(5))) {
    memset(bands, 0, count * sizeof(float));
    return;
  }
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
  analyzer_unlock();
}

void pm_audio_analyzer_get_waveform(float *samples, size_t count) {
  if (!samples) {
    return;
  }
  if (!analyzer_lock(pdMS_TO_TICKS(5))) {
    memset(samples, 0, count * sizeof(float));
    return;
  }
  const size_t n = count < PM_AUDIO_WAVE_POINTS ? count : PM_AUDIO_WAVE_POINTS;
  memcpy(samples, s_wave, n * sizeof(float));
  analyzer_unlock();
}

void pm_audio_analyzer_get_spec_history(float *rows, int count, int bands) {
  if (!rows || count <= 0 || bands <= 0) {
    return;
  }
  if (!analyzer_lock(pdMS_TO_TICKS(5))) {
    memset(rows, 0, static_cast<size_t>(count) * static_cast<size_t>(bands) * sizeof(float));
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
  analyzer_unlock();
}

float pm_audio_analyzer_get_level(void) {
  if (!analyzer_lock(pdMS_TO_TICKS(5))) {
    return 0.f;
  }
  const float level = s_level;
  analyzer_unlock();
  return level;
}

void pm_audio_analyzer_debug(PmAudioAnalyzerDebug *out) {
  if (!out) {
    return;
  }
  memset(out, 0, sizeof(*out));
  if (!analyzer_lock(pdMS_TO_TICKS(5))) {
    return;
  }
  for (int i = 0; i < PM_AUDIO_ANALYZER_IN_CHANNELS; ++i) {
    out->in_peak[i] = s_in_peak[i];
    out->in_blocks[i] = s_in_blocks[i];
  }
  out->out_peak = s_out_peak;
  out->level = s_level;
  out->out_blocks = s_out_blocks;
  analyzer_unlock();
}

void pm_audio_analyzer_aec_debug(PmAudioAecDebug *out) {
  if (!out) {
    return;
  }
  memset(out, 0, sizeof(*out));
  if (!analyzer_lock(pdMS_TO_TICKS(5))) {
    return;
  }
  out->ref_rms = s_aec_ref_rms;
  for (int i = 0; i < PM_AUDIO_ANALYZER_IN_CHANNELS; ++i) {
    out->err_rms[i] = s_aec_err_rms[i];
    out->adapt_blocks[i] = s_aec_adapt_blocks[i];
  }
  analyzer_unlock();
}

void pm_audio_analyzer_get_pitch(PmAudioPitch *out) {
  if (!out) {
    return;
  }
  memset(out, 0, sizeof(*out));
  if (!analyzer_lock(pdMS_TO_TICKS(5))) {
    return;
  }
  *out = s_pitch;
  analyzer_unlock();
}

#ifndef ASTROLABE_QEMU

bool pm_audio_analyzer_mic_begin(void) {
  if (!pm_resource_acquire(kPmResourceAnalyzer, kPmResourceVoice | kPmResourceBustFetch | kPmResourceMediaStream,
                           "audio-analyzer")) {
    return false;
  }
  if (!pm_mic_begin()) {
    pm_resource_release(kPmResourceAnalyzer, "audio-analyzer");
    return false;
  }
  return true;
}

void pm_audio_analyzer_mic_end(void) {
  pm_mic_stop();
  pm_resource_release(kPmResourceAnalyzer, "audio-analyzer");
}

void pm_audio_analyzer_tick(void) {
  static int16_t raw[512 * 4];
  static int16_t mono[512];
  static int16_t probe[PM_AUDIO_ANALYZER_IN_CHANNELS][512];
  const size_t ns = pm_mic_frame_samples();
  const int nch = pm_mic_i2s_channels();
  if (ns > sizeof(mono) / sizeof(mono[0]) || ns * static_cast<size_t>(nch) > sizeof(raw) / sizeof(raw[0])) {
    return;
  }
  size_t br = 0;
  if (pm_mic_read_frame(raw, ns, &br)) {
    float best_peak[PM_AUDIO_ANALYZER_IN_CHANNELS] = {0.f, 0.f};
    int best_src[PM_AUDIO_ANALYZER_IN_CHANNELS] = {-1, -1};
    const int scan_ch = pm_mic_capture_channels();
    for (int src = 0; src < scan_ch; ++src) {
      pm_mic_pick_capture_channel(raw, ns, src, mono);
      pm_audio_analyzer_cancel_echo_channel(src, mono, ns);
      const float peak = pcm_peak_samples(mono, ns);
      for (int dst = 0; dst < PM_AUDIO_ANALYZER_IN_CHANNELS; ++dst) {
        if (peak > best_peak[dst]) {
          for (int sh = PM_AUDIO_ANALYZER_IN_CHANNELS - 1; sh > dst; --sh) {
            best_peak[sh] = best_peak[sh - 1];
            best_src[sh] = best_src[sh - 1];
            memcpy(probe[sh], probe[sh - 1], ns * sizeof(int16_t));
          }
          best_peak[dst] = peak;
          best_src[dst] = src;
          memcpy(probe[dst], mono, ns * sizeof(int16_t));
          break;
        }
      }
    }
    for (int dst = 0; dst < PM_AUDIO_ANALYZER_IN_CHANNELS; ++dst) {
      if (best_src[dst] >= 0) {
        pm_audio_analyzer_feed_in_channel(dst, probe[dst], ns);
      }
    }
  }
  if (!analyzer_lock(pdMS_TO_TICKS(5))) {
    return;
  }
  for (int b = 0; b < PM_AUDIO_ANALYZER_BANDS; ++b) {
    s_out_disp[b] *= 0.92f;
  }
  analyzer_unlock();
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
