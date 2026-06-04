#pragma once

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
} paper_voice_result_t;

void paper_voice_result_free(paper_voice_result_t *result);

/** POST mono PCM @ 16 kHz to Castalia voice-pipeline (face=faculty). Allocates mp3 on success. */
esp_err_t paper_voice_post_pcm(const uint8_t *pcm,
                              size_t pcm_len,
                              const char *faculty_slug,
                              const char *faculty_name,
                              const char *history,
                              paper_voice_result_t *result);

esp_err_t paper_voice_play_mp3(const uint8_t *mp3, size_t mp3_len);
