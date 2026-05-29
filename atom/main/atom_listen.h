#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

typedef struct {
    int16_t *samples;
    size_t sample_count;
} atom_utterance_t;

typedef struct {
    QueueHandle_t utterance_queue;
    int16_t *capture_buf;
    size_t capture_cap_samples;
    size_t capture_len_samples;
    bool speech_active;
    uint32_t silence_frames;
    uint32_t speech_frames;
    uint32_t max_samples;
    uint32_t min_samples;
} atom_listen_t;

#define ATOM_LISTEN_FRAME_SAMPLES 320
#define ATOM_LISTEN_SILENCE_FRAMES 40
#define ATOM_LISTEN_START_FRAMES 3
#define ATOM_LISTEN_RMS_START 900
#define ATOM_LISTEN_RMS_END 450
#define ATOM_LISTEN_MAX_SECONDS 15
#define ATOM_LISTEN_MIN_MS 400

esp_err_t atom_listen_init(atom_listen_t *listen);
void atom_listen_reset(atom_listen_t *listen);
/** Feed one frame; returns true when a full utterance was queued. */
bool atom_listen_push_frame(atom_listen_t *listen, const int16_t *frame, size_t frame_samples);
