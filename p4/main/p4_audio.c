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

static const char *TAG = "astrolabe_audio";
static const int SAMPLE_RATE = 22050;
static const int CHANNELS = 1;
static const float TWO_PI = 6.28318530718f;

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
