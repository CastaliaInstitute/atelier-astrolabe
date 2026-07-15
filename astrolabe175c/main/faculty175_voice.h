#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    char transcript[320];
    char reply[768];
    char route[48];
    char faculty_slug[64];
    char faculty_name[96];
    uint8_t *mp3;
    size_t mp3_len;
    char mp3_path[64];
} faculty175_voice_result_t;

typedef esp_err_t (*faculty175_voice_tts_chunk_fn)(const uint8_t *mp3_chunk,
                                                   size_t chunk_len,
                                                   void *user);

typedef struct {
    /** Called as MP3 bytes arrive from Castalia. Return non-OK to abort the stream. */
    faculty175_voice_tts_chunk_fn on_mp3_chunk;
    void *user;
    /** Optional flash spool path for replay/debug while streaming chunks. */
    const char *spool_path;
} faculty175_voice_tts_stream_t;

typedef struct {
    const char *faculty_slug;
    const char *faculty_name;
    const char *history;
} faculty175_voice_stt_stream_config_t;

void faculty175_voice_result_free(faculty175_voice_result_t *result);

/** True when internal heap is sufficient for TLS + streaming voice pipeline work. */
bool faculty175_voice_heap_ready(const char *stage);

/** True when the configured voice endpoint can be called. reason may be NULL. */
bool faculty175_voice_config_ready(char *reason, size_t reason_cap);

/** POST mono PCM @ 16 kHz to Castalia voice-pipeline (face=faculty). Allocates mp3 on success. */
esp_err_t faculty175_voice_post_pcm(const uint8_t *pcm,
                              size_t pcm_len,
                              const char *faculty_slug,
                              const char *faculty_name,
                              const char *history,
                              faculty175_voice_result_t *result);

/** POST a text-only prompt to Castalia voice-pipeline and request spoken MP3 back. */
esp_err_t faculty175_voice_post_message(const char *message,
                                        const char *system_instruction,
                                        const char *face,
                                        const char *faculty_slug,
                                        const char *faculty_name,
                                        const char *history,
                                        faculty175_voice_result_t *result);

/** POST a text-only prompt and stream spoken MP3 chunks as the HTTP body arrives. */
esp_err_t faculty175_voice_post_message_streaming(const char *message,
                                                  const char *system_instruction,
                                                  const char *face,
                                                  const char *faculty_slug,
                                                  const char *faculty_name,
                                                  const char *history,
                                                  const faculty175_voice_tts_stream_t *stream,
                                                  faculty175_voice_result_t *result);

/** Open an indefinite STT capture session. Commit each utterance with faculty175_voice_stt_stream_commit(). */
esp_err_t faculty175_voice_stt_stream_open(const faculty175_voice_stt_stream_config_t *config);

/** Stream one mono PCM frame into the active STT session. */
esp_err_t faculty175_voice_stt_stream_write(const int16_t *pcm, size_t sample_count);

/** Finish the current utterance, send it to Castalia, and close/reset the session. */
esp_err_t faculty175_voice_stt_stream_commit(faculty175_voice_result_t *result);

/** Finish the current utterance and stream spoken MP3 reply chunks as they arrive. */
esp_err_t faculty175_voice_stt_stream_commit_streaming(const faculty175_voice_tts_stream_t *tts_stream,
                                                       faculty175_voice_result_t *result);

/** Cancel and close the active STT session. */
void faculty175_voice_stt_stream_close(void);

bool faculty175_voice_stt_stream_is_open(void);

/** Compatibility wrapper for the legacy one-utterance STT stream API. */
esp_err_t faculty175_voice_stream_begin(const char *faculty_slug, const char *faculty_name, const char *history);

/** Compatibility wrapper for faculty175_voice_stt_stream_write(). */
esp_err_t faculty175_voice_stream_write(const int16_t *pcm, size_t sample_count);

/** Compatibility wrapper for faculty175_voice_stt_stream_commit(). */
esp_err_t faculty175_voice_stream_finish(const char *faculty_slug,
                                   const char *faculty_name,
                                   faculty175_voice_result_t *result);

void faculty175_voice_stream_cancel(void);

esp_err_t faculty175_voice_play_mp3(const uint8_t *mp3, size_t mp3_len);
esp_err_t faculty175_voice_play_mp3_async(const uint8_t *mp3, size_t mp3_len);
esp_err_t faculty175_voice_play_mp3_file(const char *path, size_t mp3_len);
bool faculty175_voice_tts_playback_busy(void);

esp_err_t faculty175_voice_tts_speaker_stream_begin(void);
esp_err_t faculty175_voice_tts_speaker_stream_write(const uint8_t *mp3_chunk, size_t chunk_len);
esp_err_t faculty175_voice_tts_speaker_stream_end(void);
