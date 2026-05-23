#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

struct PmVoiceResult {
  char transcript[320];
  char reply[768];
  char route[48];
  char faculty_slug[64];
  char faculty_name[96];
  uint8_t *mp3 = nullptr;
  size_t mp3_len = 0;
};

void pm_voice_result_free(PmVoiceResult *r);

/** Short reason after a failed voice call (for UI / Serial). */
const char *pm_voice_last_error(void);

/** Probe the configured voice-pipeline host; optionally refresh DNS/WiFi before failing. */
bool pm_voice_pipeline_host_ready(bool recover);

/** POST mono LINEAR16 PCM @ 16 kHz to Supabase `voice-pipeline`. Allocates r->mp3 on success. */
bool pm_voice_post_pcm(const uint8_t *pcm, size_t pcm_len, const char *system_instruction, PmVoiceResult *r);

/**
 * Text-only turn: POST JSON { message, languageCode, optional systemInstruction }.
 * Escapes message for JSON; `system_instruction` may be null.
 */
bool pm_voice_post_message(const char *message, const char *system_instruction, PmVoiceResult *r);
bool pm_voice_post_message_ex(const char *message, const char *system_instruction, const char *face,
                              const char *faculty_slug, const char *faculty_name, PmVoiceResult *r);
bool pm_voice_post_pcm_ex(const uint8_t *pcm, size_t pcm_len, const char *system_instruction, const char *face,
                          PmVoiceResult *r);

enum class PmVoiceStatus : int8_t { Idle = 0, Working = 1, DoneOk = 2, DoneFail = -1 };

/** Non-blocking voice-pipeline request (poll with pm_voice_poll). */
bool pm_voice_begin_message(const char *message, const char *system_instruction, PmVoiceResult *r);
bool pm_voice_begin_message_ex(const char *message, const char *system_instruction, const char *face,
                               const char *faculty_slug, const char *faculty_name, PmVoiceResult *r);
bool pm_voice_begin_pcm(const uint8_t *pcm, size_t pcm_len, const char *system_instruction, PmVoiceResult *r);
bool pm_voice_begin_pcm_ex(const uint8_t *pcm, size_t pcm_len, const char *system_instruction, const char *face,
                           PmVoiceResult *r);
/** Non-blocking `voice-pipeline` with `face=clock_agenda` (spoken CalDAV brief). */
bool pm_voice_begin_clock_agenda(PmVoiceResult *r);
/** Non-blocking daily briefing (`face=daily_briefing`, raw MP3 response). */
bool pm_voice_begin_daily_briefing(PmVoiceResult *r);
/** True when the last daily briefing played audio over HTTP while downloading (no r->mp3 buffer). */
bool pm_voice_daily_briefing_streamed(void);
/** True while daily briefing is actively decoding/playing the HTTP MPEG body. */
bool pm_voice_daily_briefing_streaming_play(void);
PmVoiceStatus pm_voice_poll(void);
uint32_t pm_voice_stack_high_water(void);

/** Unblock UI if voice_net is stuck (HTTP still runs until it finishes). */
void pm_voice_abort(void);
