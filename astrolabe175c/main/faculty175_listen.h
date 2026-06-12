#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#define FACULTY175_LISTEN_WAVEFORM_LEN 128

typedef struct {
    void (*on_speech_start)(void *ctx);
    void (*on_frame)(const int16_t *frame, size_t frame_samples, void *ctx);
    /** Total mono samples captured for the segment that just ended. */
    void (*on_speech_end)(size_t sample_count, void *ctx);
    void *ctx;
} faculty175_listen_stream_cb_t;

typedef struct {
    faculty175_listen_stream_cb_t stream_cb;
    size_t capture_len_samples;
    bool speech_active;
    uint32_t silence_frames;
    uint32_t speech_frames;
    uint32_t max_samples;
    uint32_t min_samples;
    uint32_t last_rms;
    uint32_t last_peak;
    uint32_t noise_rms;
    uint32_t noise_peak;
    uint32_t settle_frames;
    uint8_t waveform[FACULTY175_LISTEN_WAVEFORM_LEN];
    /** Parallel ring: non-zero when that waveform column was streamed to STT. */
    uint8_t waveform_stream[FACULTY175_LISTEN_WAVEFORM_LEN];
    uint16_t waveform_head;
} faculty175_listen_t;

#define FACULTY175_LISTEN_FRAME_SAMPLES 320
#define FACULTY175_LISTEN_SILENCE_FRAMES 36
#define FACULTY175_LISTEN_START_FRAMES 10
#define FACULTY175_LISTEN_SETTLE_FRAMES 25
#define FACULTY175_LISTEN_RMS_START 20000
#define FACULTY175_LISTEN_RMS_END 5000
#define FACULTY175_LISTEN_PEAK_START 260
#define FACULTY175_LISTEN_PEAK_END 120
#define FACULTY175_LISTEN_MAX_SECONDS 10
#define FACULTY175_LISTEN_MIN_MS 900

esp_err_t faculty175_listen_init(faculty175_listen_t *listen);
void faculty175_listen_reset(faculty175_listen_t *listen);
void faculty175_listen_set_stream_cb(faculty175_listen_t *listen, const faculty175_listen_stream_cb_t *cb);

/** Feed one mic frame; runs VAD and invokes stream callbacks while speech is active. */
bool faculty175_listen_push_frame(faculty175_listen_t *listen, const int16_t *frame, size_t frame_samples);

/** 0–255 mic level for UI (strongest while capturing speech). */
uint8_t faculty175_listen_meter_level(const faculty175_listen_t *listen);

/** Copy recent input peaks oldest→newest into `out` (up to `len` samples). */
void faculty175_listen_waveform_copy(const faculty175_listen_t *listen, uint8_t *out, size_t len);

/** Copy stream mask aligned with faculty175_listen_waveform_copy (255 = sent to STT). */
void faculty175_listen_waveform_stream_copy(const faculty175_listen_t *listen, uint8_t *out, size_t len);

bool faculty175_listen_speech_active(const faculty175_listen_t *listen);
uint32_t faculty175_listen_last_rms(const faculty175_listen_t *listen);
