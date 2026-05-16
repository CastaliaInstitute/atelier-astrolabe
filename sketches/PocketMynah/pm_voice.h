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

/** Short reason after a failed voice call (for UI / Serial). */
const char *pm_voice_last_error(void);

/** POST mono LINEAR16 PCM @ 16 kHz to Supabase `voice-pipeline`. Allocates r->mp3 on success. */
bool pm_voice_post_pcm(const uint8_t *pcm, size_t pcm_len, const char *system_instruction, PmVoiceResult *r);

/**
 * Text-only turn: POST JSON { message, languageCode, optional systemInstruction }.
 * Escapes message for JSON; `system_instruction` may be null.
 */
bool pm_voice_post_message(const char *message, const char *system_instruction, PmVoiceResult *r);

enum class PmVoiceStatus : int8_t { Idle = 0, Working = 1, DoneOk = 2, DoneFail = -1 };

/** Non-blocking voice-pipeline request (poll with pm_voice_poll). */
bool pm_voice_begin_message(const char *message, const char *system_instruction, PmVoiceResult *r);
bool pm_voice_begin_pcm(const uint8_t *pcm, size_t pcm_len, const char *system_instruction, PmVoiceResult *r);
PmVoiceStatus pm_voice_poll(void);

/** Unblock UI if voice_net is stuck (HTTP still runs until it finishes). */
void pm_voice_abort(void);
