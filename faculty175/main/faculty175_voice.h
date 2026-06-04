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

void faculty175_voice_result_free(faculty175_voice_result_t *result);

/** True when internal heap is sufficient for TLS + streaming voice pipeline work. */
bool faculty175_voice_heap_ready(const char *stage);

/** POST mono PCM @ 16 kHz to Castalia voice-pipeline (face=faculty). Allocates mp3 on success. */
esp_err_t faculty175_voice_post_pcm(const uint8_t *pcm,
                              size_t pcm_len,
                              const char *faculty_slug,
                              const char *faculty_name,
                              const char *history,
                              faculty175_voice_result_t *result);

/** Open chunked voice-stream POST when VAD starts (metadata JSON prefix + raw PCM chunks). */
esp_err_t faculty175_voice_stream_begin(const char *faculty_slug, const char *faculty_name, const char *history);

/** Stream one mono PCM frame while speech is active. */
esp_err_t faculty175_voice_stream_write(const int16_t *pcm, size_t sample_count);

/** Finish stream, wait for pipeline response. */
esp_err_t faculty175_voice_stream_finish(const char *faculty_slug,
                                   const char *faculty_name,
                                   faculty175_voice_result_t *result);

void faculty175_voice_stream_cancel(void);

esp_err_t faculty175_voice_play_mp3(const uint8_t *mp3, size_t mp3_len);
esp_err_t faculty175_voice_play_mp3_file(const char *path, size_t mp3_len);
