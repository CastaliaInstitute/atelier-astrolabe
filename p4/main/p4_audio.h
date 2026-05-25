#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  bool speaker_ready;
  bool mic_ready;
  int sample_rate;
  int channels;
} astrolabe_p4_audio_status_t;

esp_err_t astrolabe_p4_audio_init(void);
bool astrolabe_p4_audio_play_tone(int hz, int duration_ms);
bool astrolabe_p4_audio_probe_mic(void);
bool astrolabe_p4_audio_capture_pcm(int sample_rate, int duration_ms, int16_t **out_pcm, size_t *out_samples,
                                    int *out_peak, int64_t *out_avg_energy);
bool astrolabe_p4_audio_capture_vad(int sample_rate, int max_ms, int16_t **out_pcm, size_t *out_samples,
                                    int *out_peak, int64_t *out_avg_energy);
bool astrolabe_p4_audio_play_mp3(const uint8_t *mp3, size_t mp3_len);
void astrolabe_p4_audio_log_status(void);
astrolabe_p4_audio_status_t astrolabe_p4_audio_status(void);

#ifdef __cplusplus
}
#endif
