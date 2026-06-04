#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

typedef struct {
    int16_t *samples;
    size_t sample_count;
} paper_utterance_t;

#define PAPER_LISTEN_WAVEFORM_LEN 128

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
    uint32_t last_rms;
    uint8_t waveform[PAPER_LISTEN_WAVEFORM_LEN];
    uint16_t waveform_head;
} paper_listen_t;

#define PAPER_LISTEN_FRAME_SAMPLES 320
#define PAPER_LISTEN_SILENCE_FRAMES 40
#define PAPER_LISTEN_START_FRAMES 3
#define PAPER_LISTEN_RMS_START 550
#define PAPER_LISTEN_RMS_END 280
#define PAPER_LISTEN_MAX_SECONDS 15
#define PAPER_LISTEN_MIN_MS 400

esp_err_t paper_listen_init(paper_listen_t *listen);
void paper_listen_reset(paper_listen_t *listen);
/** Feed one frame; returns true when a full utterance was queued. */
bool paper_listen_push_frame(paper_listen_t *listen, const int16_t *frame, size_t frame_samples);

/** 0-255 mic level for UI (strongest while capturing speech). */
uint8_t paper_listen_meter_level(const paper_listen_t *listen);

/** Copy recent input peaks oldest->newest into `out` (up to `len` samples). */
void paper_listen_waveform_copy(const paper_listen_t *listen, uint8_t *out, size_t len);

bool paper_listen_speech_active(const paper_listen_t *listen);
uint32_t paper_listen_last_rms(const paper_listen_t *listen);
