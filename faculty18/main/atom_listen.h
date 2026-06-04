#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#define ATOM_LISTEN_WAVEFORM_LEN 128

typedef struct {
    void (*on_speech_start)(void *ctx);
    void (*on_frame)(const int16_t *frame, size_t frame_samples, void *ctx);
    /** Total mono samples captured for the segment that just ended. */
    void (*on_speech_end)(size_t sample_count, void *ctx);
    void *ctx;
} atom_listen_stream_cb_t;

typedef struct {
    atom_listen_stream_cb_t stream_cb;
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
#define ATOM_LISTEN_SILENCE_FRAMES 35
#define ATOM_LISTEN_START_FRAMES 6
#define ATOM_LISTEN_RMS_START 1400
#define ATOM_LISTEN_RMS_END 420
#define ATOM_LISTEN_MAX_SECONDS 10
#define ATOM_LISTEN_MIN_MS 400

esp_err_t atom_listen_init(atom_listen_t *listen);
void atom_listen_reset(atom_listen_t *listen);
void atom_listen_set_stream_cb(atom_listen_t *listen, const atom_listen_stream_cb_t *cb);

/** Feed one mic frame; runs VAD and invokes stream callbacks while speech is active. */
bool atom_listen_push_frame(atom_listen_t *listen, const int16_t *frame, size_t frame_samples);

/** 0–255 mic level for UI (strongest while capturing speech). */
uint8_t atom_listen_meter_level(const atom_listen_t *listen);

/** Copy recent input peaks oldest→newest into `out` (up to `len` samples). */
void atom_listen_waveform_copy(const atom_listen_t *listen, uint8_t *out, size_t len);

bool atom_listen_speech_active(const atom_listen_t *listen);
uint32_t atom_listen_last_rms(const atom_listen_t *listen);
