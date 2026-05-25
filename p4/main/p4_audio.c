#include "p4_audio.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "bsp/esp-bsp.h"
#include "esp_check.h"
#include "esp_codec_dev.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"

static const char *TAG = "astrolabe_audio";
static const int SAMPLE_RATE = 22050;
static const int CHANNELS = 1;
static const float TWO_PI = 6.28318530718f;
static const int VOICE_SAMPLE_RATE = 16000;

static esp_codec_dev_handle_t s_speaker;
static esp_codec_dev_handle_t s_mic;
static bool s_speaker_ready;
static bool s_mic_ready;

esp_err_t astrolabe_p4_audio_init(void) {
  if (s_speaker_ready && s_mic_ready) {
    return ESP_OK;
  }

  if (!s_speaker_ready) {
    s_speaker = bsp_audio_codec_speaker_init();
    ESP_RETURN_ON_FALSE(s_speaker != NULL, ESP_FAIL, TAG, "speaker codec init");
    ESP_RETURN_ON_FALSE(esp_codec_dev_set_out_vol(s_speaker, 55) == ESP_CODEC_DEV_OK, ESP_FAIL, TAG,
                        "speaker volume");
    s_speaker_ready = true;
    ESP_LOGI(TAG, "speaker codec ready: ES8311 via I2S, pa gpio=%d", (int)BSP_POWER_AMP_IO);
  }

  if (!s_mic_ready) {
    s_mic = bsp_audio_codec_microphone_init();
    ESP_RETURN_ON_FALSE(s_mic != NULL, ESP_FAIL, TAG, "microphone codec init");
    s_mic_ready = true;
    ESP_LOGI(TAG, "microphone codec ready: ES7210 via I2S");
  }
  return ESP_OK;
}

bool astrolabe_p4_audio_play_tone(int hz, int duration_ms) {
  if (hz < 80 || hz > 4000) {
    hz = 660;
  }
  if (duration_ms < 50 || duration_ms > 5000) {
    duration_ms = 350;
  }
  if (astrolabe_p4_audio_init() != ESP_OK) {
    return false;
  }

  esp_codec_dev_sample_info_t fs = {
      .bits_per_sample = 16,
      .channel = CHANNELS,
      .sample_rate = SAMPLE_RATE,
  };
  if (esp_codec_dev_open(s_speaker, &fs) != ESP_CODEC_DEV_OK) {
    ESP_LOGE(TAG, "speaker open failed");
    return false;
  }

  const int frames_per_chunk = 256;
  int16_t *pcm = heap_caps_malloc(frames_per_chunk * sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (pcm == NULL) {
    (void)esp_codec_dev_close(s_speaker);
    return false;
  }

  const int total_frames = (SAMPLE_RATE * duration_ms) / 1000;
  float phase = 0.0f;
  const float step = TWO_PI * (float)hz / (float)SAMPLE_RATE;
  int remaining = total_frames;
  bool ok = true;
  while (remaining > 0) {
    const int frames = remaining > frames_per_chunk ? frames_per_chunk : remaining;
    for (int i = 0; i < frames; ++i) {
      float env = 0.55f;
      if (remaining < SAMPLE_RATE / 80) {
        env *= (float)remaining / (float)(SAMPLE_RATE / 80);
      }
      pcm[i] = (int16_t)(sinf(phase) * 18000.0f * env);
      phase += step;
      if (phase >= TWO_PI) {
        phase -= TWO_PI;
      }
    }
    if (esp_codec_dev_write(s_speaker, pcm, frames * (int)sizeof(int16_t)) != ESP_CODEC_DEV_OK) {
      ok = false;
      break;
    }
    remaining -= frames;
  }

  memset(pcm, 0, frames_per_chunk * sizeof(int16_t));
  for (int i = 0; i < 4; ++i) {
    (void)esp_codec_dev_write(s_speaker, pcm, frames_per_chunk * (int)sizeof(int16_t));
  }
  free(pcm);
  vTaskDelay(pdMS_TO_TICKS(120));
  (void)esp_codec_dev_close(s_speaker);
  ESP_LOGI(TAG, "audio tone hz=%d duration_ms=%d ok=%d", hz, duration_ms, ok);
  return ok;
}

bool astrolabe_p4_audio_probe_mic(void) {
  if (astrolabe_p4_audio_init() != ESP_OK) {
    return false;
  }
  esp_codec_dev_sample_info_t fs = {
      .bits_per_sample = 16,
      .channel = CHANNELS,
      .sample_rate = SAMPLE_RATE,
  };
  if (esp_codec_dev_open(s_mic, &fs) != ESP_CODEC_DEV_OK) {
    ESP_LOGE(TAG, "mic open failed");
    return false;
  }

  int16_t samples[512] = {};
  ESP_LOGI(TAG, "audio mic probe reading %u samples", (unsigned)(sizeof(samples) / sizeof(samples[0])));
  if (esp_codec_dev_read(s_mic, samples, sizeof(samples)) != ESP_CODEC_DEV_OK) {
    (void)esp_codec_dev_close(s_mic);
    ESP_LOGE(TAG, "mic read failed");
    return false;
  }
  (void)esp_codec_dev_close(s_mic);

  int peak = 0;
  int64_t energy = 0;
  for (size_t i = 0; i < sizeof(samples) / sizeof(samples[0]); ++i) {
    int value = samples[i];
    if (value < 0) {
      value = -value;
    }
    if (value > peak) {
      peak = value;
    }
    energy += (int64_t)samples[i] * (int64_t)samples[i];
  }
  ESP_LOGI(TAG, "audio mic probe samples=%u peak=%d avg_energy=%lld", (unsigned)(sizeof(samples) / sizeof(samples[0])),
           peak, (long long)(energy / (int64_t)(sizeof(samples) / sizeof(samples[0]))));
  return true;
}

static void audio_stats(const int16_t *samples, size_t count, int *out_peak, int64_t *out_avg_energy) {
  int peak = 0;
  int64_t energy = 0;
  for (size_t i = 0; i < count; ++i) {
    int value = samples[i];
    if (value < 0) {
      value = -value;
    }
    if (value > peak) {
      peak = value;
    }
    energy += (int64_t)samples[i] * (int64_t)samples[i];
  }
  if (out_peak != NULL) {
    *out_peak = peak;
  }
  if (out_avg_energy != NULL) {
    *out_avg_energy = count > 0 ? energy / (int64_t)count : 0;
  }
}

bool astrolabe_p4_audio_capture_pcm(int sample_rate, int duration_ms, int16_t **out_pcm, size_t *out_samples,
                                    int *out_peak, int64_t *out_avg_energy) {
  if (out_pcm == NULL || out_samples == NULL) {
    return false;
  }
  *out_pcm = NULL;
  *out_samples = 0;
  if (sample_rate <= 0) {
    sample_rate = VOICE_SAMPLE_RATE;
  }
  if (duration_ms < 250) {
    duration_ms = 250;
  }
  if (duration_ms > 12000) {
    duration_ms = 12000;
  }
  if (astrolabe_p4_audio_init() != ESP_OK) {
    return false;
  }

  const size_t total_samples = ((size_t)sample_rate * (size_t)duration_ms) / 1000u;
  int16_t *pcm = heap_caps_malloc(total_samples * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (pcm == NULL) {
    pcm = heap_caps_malloc(total_samples * sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  }
  if (pcm == NULL) {
    ESP_LOGE(TAG, "capture oom samples=%u", (unsigned)total_samples);
    return false;
  }

  esp_codec_dev_sample_info_t fs = {
      .bits_per_sample = 16,
      .channel = CHANNELS,
      .sample_rate = sample_rate,
  };
  if (esp_codec_dev_open(s_mic, &fs) != ESP_CODEC_DEV_OK) {
    ESP_LOGE(TAG, "mic open failed");
    free(pcm);
    return false;
  }

  ESP_LOGI(TAG, "audio capture begin sample_rate=%d ms=%d samples=%u", sample_rate, duration_ms,
           (unsigned)total_samples);
  size_t done = 0;
  bool ok = true;
  while (done < total_samples) {
    size_t chunk = total_samples - done;
    if (chunk > 4096) {
      chunk = 4096;
    }
    if (esp_codec_dev_read(s_mic, pcm + done, (int)(chunk * sizeof(int16_t))) != ESP_CODEC_DEV_OK) {
      ok = false;
      break;
    }
    done += chunk;
  }
  (void)esp_codec_dev_close(s_mic);

  if (!ok || done == 0) {
    ESP_LOGE(TAG, "mic capture failed done=%u", (unsigned)done);
    free(pcm);
    return false;
  }
  audio_stats(pcm, done, out_peak, out_avg_energy);
  *out_pcm = pcm;
  *out_samples = done;
  ESP_LOGI(TAG, "audio capture sample_rate=%d ms=%d samples=%u peak=%d avg_energy=%lld", sample_rate, duration_ms,
           (unsigned)done, out_peak ? *out_peak : -1, out_avg_energy ? (long long)*out_avg_energy : -1);
  return true;
}

bool astrolabe_p4_audio_capture_vad(int sample_rate, int max_ms, int16_t **out_pcm, size_t *out_samples,
                                    int *out_peak, int64_t *out_avg_energy) {
  if (out_pcm == NULL || out_samples == NULL) {
    return false;
  }
  *out_pcm = NULL;
  *out_samples = 0;
  if (sample_rate <= 0) {
    sample_rate = VOICE_SAMPLE_RATE;
  }
  if (max_ms < 1000) {
    max_ms = 1000;
  }
  if (max_ms > 15000) {
    max_ms = 15000;
  }
  if (astrolabe_p4_audio_init() != ESP_OK) {
    return false;
  }

  const size_t max_samples = ((size_t)sample_rate * (size_t)max_ms) / 1000u;
  int16_t *pcm = heap_caps_malloc(max_samples * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (pcm == NULL) {
    pcm = heap_caps_malloc(max_samples * sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  }
  if (pcm == NULL) {
    return false;
  }

  esp_codec_dev_sample_info_t fs = {
      .bits_per_sample = 16,
      .channel = CHANNELS,
      .sample_rate = sample_rate,
  };
  if (esp_codec_dev_open(s_mic, &fs) != ESP_CODEC_DEV_OK) {
    free(pcm);
    return false;
  }

  const size_t chunk = (size_t)sample_rate / 50u;  // 20 ms
  int16_t frame[512];
  size_t done = 0;
  int noise_peak = 0;
  int voice_frames = 0;
  int silence_frames = 0;
  bool triggered = false;
  const int max_frames = max_ms / 20;
  for (int frame_idx = 0; frame_idx < max_frames && done + chunk <= max_samples; ++frame_idx) {
    if (esp_codec_dev_read(s_mic, frame, (int)(chunk * sizeof(int16_t))) != ESP_CODEC_DEV_OK) {
      break;
    }
    int peak = 0;
    int64_t energy = 0;
    audio_stats(frame, chunk, &peak, &energy);
    if (!triggered && frame_idx < 25) {
      noise_peak = (noise_peak * 7 + peak) / 8;
    }
    const int threshold = noise_peak > 300 ? noise_peak * 3 : 900;
    const bool voiced = peak > threshold || energy > 550000;
    if (!triggered) {
      if (voiced) {
        voice_frames++;
      } else if (voice_frames > 0) {
        voice_frames--;
      }
      if (voice_frames >= 3) {
        triggered = true;
        ESP_LOGI(TAG, "vad triggered peak=%d threshold=%d", peak, threshold);
      }
      continue;
    }
    memcpy(pcm + done, frame, chunk * sizeof(int16_t));
    done += chunk;
    if (voiced) {
      silence_frames = 0;
    } else if (++silence_frames >= 35 && done > (size_t)sample_rate / 2u) {
      break;
    }
  }
  (void)esp_codec_dev_close(s_mic);

  if (!triggered || done < (size_t)sample_rate / 4u) {
    free(pcm);
    ESP_LOGW(TAG, "vad capture: no speech");
    return false;
  }
  audio_stats(pcm, done, out_peak, out_avg_energy);
  *out_pcm = pcm;
  *out_samples = done;
  ESP_LOGI(TAG, "vad capture samples=%u peak=%d avg_energy=%lld", (unsigned)done, out_peak ? *out_peak : -1,
           out_avg_energy ? (long long)*out_avg_energy : -1);
  return true;
}

bool astrolabe_p4_audio_play_mp3(const uint8_t *mp3, size_t mp3_len) {
  if (mp3 == NULL || mp3_len < 32 || astrolabe_p4_audio_init() != ESP_OK) {
    return false;
  }

  mp3dec_t dec;
  mp3dec_init(&dec);
  const uint8_t *buf = mp3;
  int bytes_left = (int)mp3_len;
  bool opened = false;
  int out_hz = 0;
  int out_channels = 0;
  uint32_t frames = 0;
  static int16_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
  static int16_t mono[MINIMP3_MAX_SAMPLES_PER_FRAME];

  while (bytes_left > 0) {
    mp3dec_frame_info_t info = {};
    int samples_per_ch = mp3dec_decode_frame(&dec, buf, bytes_left, pcm, &info);
    if (info.frame_bytes <= 0) {
      int skip = 1;
      if (bytes_left >= 10 && buf[0] == 'I' && buf[1] == 'D' && buf[2] == '3') {
        skip = ((buf[6] & 0x7f) << 21) | ((buf[7] & 0x7f) << 14) | ((buf[8] & 0x7f) << 7) | (buf[9] & 0x7f);
        if (skip < 10) {
          skip = 10;
        }
      }
      if (skip > bytes_left) {
        skip = bytes_left;
      }
      buf += skip;
      bytes_left -= skip;
      continue;
    }
    buf += info.frame_bytes;
    bytes_left -= info.frame_bytes;
    if (samples_per_ch <= 0 || info.hz <= 0 || info.channels <= 0) {
      continue;
    }
    if (!opened) {
      out_hz = info.hz;
      out_channels = 1;
      esp_codec_dev_sample_info_t fs = {
          .bits_per_sample = 16,
          .channel = out_channels,
          .sample_rate = out_hz,
      };
      if (esp_codec_dev_open(s_speaker, &fs) != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "speaker open for mp3 failed");
        return false;
      }
      opened = true;
    }

    const size_t n = (size_t)samples_per_ch;
    if (info.channels == 1) {
      if (esp_codec_dev_write(s_speaker, pcm, (int)(n * sizeof(int16_t))) != ESP_CODEC_DEV_OK) {
        break;
      }
    } else {
      for (size_t i = 0; i < n; ++i) {
        mono[i] = (int16_t)(((int)pcm[i * 2] + (int)pcm[i * 2 + 1]) / 2);
      }
      if (esp_codec_dev_write(s_speaker, mono, (int)(n * sizeof(int16_t))) != ESP_CODEC_DEV_OK) {
        break;
      }
    }
    frames += (uint32_t)n;
    if (out_hz > 0 && frames / (uint32_t)out_hz > 180u) {
      break;
    }
  }

  if (opened) {
    int16_t silence[256] = {};
    for (int i = 0; i < 6; ++i) {
      (void)esp_codec_dev_write(s_speaker, silence, sizeof(silence));
    }
    vTaskDelay(pdMS_TO_TICKS(120));
    (void)esp_codec_dev_close(s_speaker);
  }
  ESP_LOGI(TAG, "mp3 playback ok=%d bytes=%u frames=%lu hz=%d", opened, (unsigned)mp3_len, (unsigned long)frames,
           out_hz);
  return opened;
}

void astrolabe_p4_audio_log_status(void) {
  astrolabe_p4_audio_status_t st = astrolabe_p4_audio_status();
  ESP_LOGI(TAG, "audio status speaker=%d mic=%d sample_rate=%d channels=%d", st.speaker_ready, st.mic_ready,
           st.sample_rate, st.channels);
}

astrolabe_p4_audio_status_t astrolabe_p4_audio_status(void) {
  return (astrolabe_p4_audio_status_t){
      .speaker_ready = s_speaker_ready,
      .mic_ready = s_mic_ready,
      .sample_rate = SAMPLE_RATE,
      .channels = CHANNELS,
  };
}
