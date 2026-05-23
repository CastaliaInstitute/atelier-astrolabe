#pragma once

#include <stdbool.h>

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
void astrolabe_p4_audio_log_status(void);
astrolabe_p4_audio_status_t astrolabe_p4_audio_status(void);

#ifdef __cplusplus
}
#endif
