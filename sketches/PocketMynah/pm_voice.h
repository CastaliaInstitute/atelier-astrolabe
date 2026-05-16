#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

struct PmVoiceResult {
  char transcript[320];
  char reply[768];
  uint8_t *mp3 = nullptr;
  size_t mp3_len = 0;
};

void pm_voice_result_free(PmVoiceResult *r);

/** POST mono LINEAR16 PCM @ 16 kHz to Supabase `voice-pipeline`. Allocates r->mp3 on success. */
bool pm_voice_post_pcm(const uint8_t *pcm, size_t pcm_len, PmVoiceResult *r);
