#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

typedef struct {
    int16_t *samples;
    size_t sample_count;
} atom_utterance_t;

#define ATOM_LISTEN_WAVEFORM_LEN 128

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
    uint8_t waveform[ATOM_LISTEN_WAVEFORM_LEN];
    uint16_t waveform_head;
} atom_listen_t;

#define ATOM_LISTEN_FRAME_SAMPLES 320
#define ATOM_LISTEN_SILENCE_FRAMES 40
#define ATOM_LISTEN_START_FRAMES 3
#define ATOM_LISTEN_RMS_START 550
#define ATOM_LISTEN_RMS_END 280
#define ATOM_LISTEN_MAX_SECONDS 15
#define ATOM_LISTEN_MIN_MS 400

esp_err_t atom_listen_init(atom_listen_t *listen);
void atom_listen_reset(atom_listen_t *listen);
/** Feed one frame; returns true when a full utterance was queued. */
bool atom_listen_push_frame(atom_listen_t *listen, const int16_t *frame, size_t frame_samples);

/** 0–255 mic level for UI (strongest while capturing speech). */
uint8_t atom_listen_meter_level(const atom_listen_t *listen);

/** Copy recent input peaks oldest→newest into `out` (up to `len` samples). */
void atom_listen_waveform_copy(const atom_listen_t *listen, uint8_t *out, size_t len);

bool atom_listen_speech_active(const atom_listen_t *listen);
uint32_t atom_listen_last_rms(const atom_listen_t *listen);
