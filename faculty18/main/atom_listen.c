#include "atom_listen.h"

#include <string.h>

#include "esp_log.h"

#include "atom_log.h"

static const char *TAG = "atom_listen";

static uint32_t frame_rms(const int16_t *frame, size_t count)
{
    uint64_t acc = 0;
    for (size_t i = 0; i < count; ++i) {
        const int32_t s = frame[i];
        acc += (uint64_t)(s * s);
    }
    if (count == 0) {
        return 0;
    }
    return (uint32_t)(acc / count);
}

static uint8_t frame_peak_level(const int16_t *frame, size_t count)
{
    int32_t peak = 0;
    for (size_t i = 0; i < count; ++i) {
        int32_t sample = frame[i];
        if (sample < 0) {
            sample = -sample;
        }
        if (sample > peak) {
            peak = sample;
        }
    }
    const int32_t ceiling = 2200;
    if (peak > ceiling) {
        peak = ceiling;
    }
    return (uint8_t)((peak * 255) / ceiling);
}

static uint8_t frame_rms_level(uint32_t rms)
{
    const uint32_t floor = 40;
    const uint32_t ceiling = 2200;
    if (rms <= floor) {
        return 0;
    }
    if (rms >= ceiling) {
        return 255;
    }
    return (uint8_t)(((rms - floor) * 255u) / (ceiling - floor));
}

static uint8_t frame_wave_level(const int16_t *frame, size_t count, uint32_t rms)
{
    const uint8_t peak = frame_peak_level(frame, count);
    const uint8_t rms_level = frame_rms_level(rms);
    return peak > rms_level ? peak : rms_level;
}

static void waveform_push(atom_listen_t *listen, uint8_t level)
{
    if (listen == NULL) {
        return;
    }
    listen->waveform[listen->waveform_head] = level;
    listen->waveform_head = (uint16_t)((listen->waveform_head + 1) % ATOM_LISTEN_WAVEFORM_LEN);
}

static void speech_segment_end(atom_listen_t *listen, bool deliver)
{
    const size_t samples = listen->capture_len_samples;
    const uint32_t min_samples = listen->min_samples;
    const atom_listen_stream_cb_t cb = listen->stream_cb;
    atom_listen_reset(listen);
    if (deliver && cb.on_speech_end != NULL && samples >= min_samples) {
        cb.on_speech_end(samples, cb.ctx);
    } else if (deliver && samples > 0) {
        ATOM_LOG_STAGE(TAG, "capture", "segment too short %.0fms — dropped",
                       (float)samples * 1000.f / 16000.f);
    }
}

esp_err_t atom_listen_init(atom_listen_t *listen)
{
    if (listen == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(listen, 0, sizeof(*listen));
    listen->max_samples = ATOM_LISTEN_MAX_SECONDS * 16000;
    listen->min_samples = (ATOM_LISTEN_MIN_MS * 16000) / 1000;
    listen->waveform_head = 0;
    memset(listen->waveform, 0, sizeof(listen->waveform));
    atom_listen_reset(listen);
    ESP_LOGI(TAG, "listen ready max=%u samples (stream on VAD)", (unsigned)listen->max_samples);
    return ESP_OK;
}

void atom_listen_reset(atom_listen_t *listen)
{
    if (listen == NULL) {
        return;
    }
    listen->capture_len_samples = 0;
    listen->speech_active = false;
    listen->silence_frames = 0;
    listen->speech_frames = 0;
    listen->last_rms = 0;
}

void atom_listen_set_stream_cb(atom_listen_t *listen, const atom_listen_stream_cb_t *cb)
{
    if (listen == NULL) {
        return;
    }
    if (cb == NULL) {
        memset(&listen->stream_cb, 0, sizeof(listen->stream_cb));
        return;
    }
    listen->stream_cb = *cb;
}

uint8_t atom_listen_meter_level(const atom_listen_t *listen)
{
    if (listen == NULL || listen->last_rms == 0) {
        return 0;
    }
    const uint32_t floor = listen->speech_active ? ATOM_LISTEN_RMS_END : 80;
    const uint32_t ceiling = listen->speech_active ? 9000 : 2200;
    uint32_t rms = listen->last_rms;
    if (rms <= floor) {
        return 0;
    }
    if (rms >= ceiling) {
        return 255;
    }
    return (uint8_t)(((rms - floor) * 255u) / (ceiling - floor));
}

bool atom_listen_speech_active(const atom_listen_t *listen)
{
    return listen != NULL && listen->speech_active;
}

uint32_t atom_listen_last_rms(const atom_listen_t *listen)
{
    if (listen == NULL) {
        return 0;
    }
    return listen->last_rms;
}

void atom_listen_waveform_copy(const atom_listen_t *listen, uint8_t *out, size_t len)
{
    if (listen == NULL || out == NULL || len == 0) {
        return;
    }
    const size_t n = len < ATOM_LISTEN_WAVEFORM_LEN ? len : ATOM_LISTEN_WAVEFORM_LEN;
    const uint16_t head = listen->waveform_head;
    for (size_t i = 0; i < n; ++i) {
        out[i] = listen->waveform[(head + i) % ATOM_LISTEN_WAVEFORM_LEN];
    }
    if (len > n) {
        memset(out + n, 0, len - n);
    }
}

bool atom_listen_push_frame(atom_listen_t *listen, const int16_t *frame, size_t frame_samples)
{
    if (listen == NULL || frame == NULL || frame_samples == 0) {
        return false;
    }

    const uint32_t rms = frame_rms(frame, frame_samples);
    listen->last_rms = rms;
    waveform_push(listen, frame_wave_level(frame, frame_samples, rms));
    const bool voiced = rms >= (listen->speech_active ? ATOM_LISTEN_RMS_END : ATOM_LISTEN_RMS_START);

    if (!listen->speech_active) {
        if (voiced) {
            listen->speech_frames++;
            if (listen->speech_frames >= ATOM_LISTEN_START_FRAMES) {
                listen->speech_active = true;
                listen->silence_frames = 0;
                listen->capture_len_samples = 0;
                ATOM_LOG_STAGE(TAG, "capture", "VAD start — streaming");
                if (listen->stream_cb.on_speech_start != NULL) {
                    listen->stream_cb.on_speech_start(listen->stream_cb.ctx);
                }
            }
        } else {
            listen->speech_frames = 0;
        }
        return false;
    }

    if (listen->stream_cb.on_frame != NULL) {
        listen->stream_cb.on_frame(frame, frame_samples, listen->stream_cb.ctx);
    }
    listen->capture_len_samples += frame_samples;

    if (listen->capture_len_samples >= listen->max_samples) {
        ESP_LOGW(TAG, "utterance max length reached");
        speech_segment_end(listen, true);
        return true;
    }

    if (voiced) {
        listen->silence_frames = 0;
    } else {
        listen->silence_frames++;
        if (listen->silence_frames >= ATOM_LISTEN_SILENCE_FRAMES) {
            const float dur_s = (float)listen->capture_len_samples / 16000.f;
            ATOM_LOG_STAGE(TAG, "capture", "VAD end — streamed %.2fs (%u samples)", dur_s,
                           (unsigned)listen->capture_len_samples);
            speech_segment_end(listen, true);
            return true;
        }
    }
    return false;
}
