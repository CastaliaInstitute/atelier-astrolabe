#include "faculty175_listen.h"

#include <string.h>

#include "esp_log.h"

#include "faculty175_log.h"

static const char *TAG = "faculty175_listen";

static uint32_t frame_rms(const int16_t *frame, size_t count)
{
    if (count == 0) {
        return 0;
    }
    int64_t sum = 0;
    for (size_t i = 0; i < count; ++i) {
        sum += frame[i];
    }
    const int32_t mean = (int32_t)(sum / (int64_t)count);
    uint64_t acc = 0;
    for (size_t i = 0; i < count; ++i) {
        const int32_t s = (int32_t)frame[i] - mean;
        acc += (uint64_t)(s * s);
    }
    return (uint32_t)(acc / count);
}

static uint32_t frame_peak(const int16_t *frame, size_t count)
{
    if (count == 0) {
        return 0;
    }
    int64_t sum = 0;
    for (size_t i = 0; i < count; ++i) {
        sum += frame[i];
    }
    const int32_t mean = (int32_t)(sum / (int64_t)count);
    int32_t peak = 0;
    for (size_t i = 0; i < count; ++i) {
        int32_t sample = (int32_t)frame[i] - mean;
        if (sample < 0) {
            sample = -sample;
        }
        if (sample > peak) {
            peak = sample;
        }
    }
    return (uint32_t)peak;
}

static uint8_t frame_peak_level(uint32_t peak)
{
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

static uint8_t frame_wave_level(uint32_t peak, uint32_t rms)
{
    const uint8_t peak_level = frame_peak_level(peak);
    const uint8_t rms_level = frame_rms_level(rms);
    return peak_level > rms_level ? peak_level : rms_level;
}

static void noise_floor_update(faculty175_listen_t *listen, uint32_t rms, uint32_t peak, bool fast)
{
    if (listen == NULL) {
        return;
    }
    if (listen->noise_rms == 0) {
        listen->noise_rms = rms;
    } else if (fast) {
        listen->noise_rms = (listen->noise_rms * 3u + rms) / 4u;
    } else {
        listen->noise_rms = (listen->noise_rms * 31u + rms) / 32u;
    }

    if (listen->noise_peak == 0) {
        listen->noise_peak = peak;
    } else if (fast) {
        listen->noise_peak = (listen->noise_peak * 3u + peak) / 4u;
    } else {
        listen->noise_peak = (listen->noise_peak * 31u + peak) / 32u;
    }
}

static uint32_t adaptive_rms_start(const faculty175_listen_t *listen)
{
    if (listen == NULL || listen->noise_rms == 0) {
        return FACULTY175_LISTEN_RMS_START;
    }
    const uint32_t adaptive = listen->noise_rms * 2u + 5000u;
    return adaptive > FACULTY175_LISTEN_RMS_START ? adaptive : FACULTY175_LISTEN_RMS_START;
}

static uint32_t adaptive_peak_start(const faculty175_listen_t *listen)
{
    if (listen == NULL || listen->noise_peak == 0) {
        return FACULTY175_LISTEN_PEAK_START;
    }
    const uint32_t adaptive = listen->noise_peak * 2u + 120u;
    return adaptive > FACULTY175_LISTEN_PEAK_START ? adaptive : FACULTY175_LISTEN_PEAK_START;
}

static void waveform_push(faculty175_listen_t *listen, uint8_t level, bool streamed)
{
    if (listen == NULL) {
        return;
    }
    listen->waveform[listen->waveform_head] = level;
    listen->waveform_stream[listen->waveform_head] = streamed ? 255 : 0;
    listen->waveform_head = (uint16_t)((listen->waveform_head + 1) % FACULTY175_LISTEN_WAVEFORM_LEN);
}

static void speech_segment_end(faculty175_listen_t *listen, bool deliver)
{
    const size_t samples = listen->capture_len_samples;
    const uint32_t min_samples = listen->min_samples;
    const faculty175_listen_stream_cb_t cb = listen->stream_cb;
    faculty175_listen_reset(listen);
    if (deliver && cb.on_speech_end != NULL && samples >= min_samples) {
        cb.on_speech_end(samples, cb.ctx);
    } else if (deliver && samples > 0) {
        FACULTY175_LOG_STAGE(TAG, "capture", "segment too short %.0fms — dropped",
                       (float)samples * 1000.f / 16000.f);
    }
}

esp_err_t faculty175_listen_init(faculty175_listen_t *listen)
{
    if (listen == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(listen, 0, sizeof(*listen));
    listen->max_samples = FACULTY175_LISTEN_MAX_SECONDS * 16000;
    listen->min_samples = (FACULTY175_LISTEN_MIN_MS * 16000) / 1000;
    listen->waveform_head = 0;
    memset(listen->waveform, 0, sizeof(listen->waveform));
    memset(listen->waveform_stream, 0, sizeof(listen->waveform_stream));
    faculty175_listen_reset(listen);
    ESP_LOGI(TAG, "listen ready max=%u samples (stream on VAD)", (unsigned)listen->max_samples);
    return ESP_OK;
}

void faculty175_listen_reset(faculty175_listen_t *listen)
{
    if (listen == NULL) {
        return;
    }
    listen->capture_len_samples = 0;
    listen->speech_active = false;
    listen->silence_frames = 0;
    listen->speech_frames = 0;
    listen->last_rms = 0;
    listen->last_peak = 0;
    listen->settle_frames = FACULTY175_LISTEN_SETTLE_FRAMES;
}

void faculty175_listen_set_stream_cb(faculty175_listen_t *listen, const faculty175_listen_stream_cb_t *cb)
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

uint8_t faculty175_listen_meter_level(const faculty175_listen_t *listen)
{
    if (listen == NULL || listen->last_rms == 0) {
        return 0;
    }
    const uint32_t floor = listen->speech_active ? FACULTY175_LISTEN_RMS_END : 80;
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

bool faculty175_listen_speech_active(const faculty175_listen_t *listen)
{
    return listen != NULL && listen->speech_active;
}

uint32_t faculty175_listen_last_rms(const faculty175_listen_t *listen)
{
    if (listen == NULL) {
        return 0;
    }
    return listen->last_rms;
}

void faculty175_listen_waveform_copy(const faculty175_listen_t *listen, uint8_t *out, size_t len)
{
    if (listen == NULL || out == NULL || len == 0) {
        return;
    }
    const size_t n = len < FACULTY175_LISTEN_WAVEFORM_LEN ? len : FACULTY175_LISTEN_WAVEFORM_LEN;
    const uint16_t head = listen->waveform_head;
    for (size_t i = 0; i < n; ++i) {
        out[i] = listen->waveform[(head + i) % FACULTY175_LISTEN_WAVEFORM_LEN];
    }
    if (len > n) {
        memset(out + n, 0, len - n);
    }
}

void faculty175_listen_waveform_stream_copy(const faculty175_listen_t *listen, uint8_t *out, size_t len)
{
    if (listen == NULL || out == NULL || len == 0) {
        return;
    }
    const size_t n = len < FACULTY175_LISTEN_WAVEFORM_LEN ? len : FACULTY175_LISTEN_WAVEFORM_LEN;
    const uint16_t head = listen->waveform_head;
    for (size_t i = 0; i < n; ++i) {
        out[i] = listen->waveform_stream[(head + i) % FACULTY175_LISTEN_WAVEFORM_LEN];
    }
    if (len > n) {
        memset(out + n, 0, len - n);
    }
}

bool faculty175_listen_push_frame(faculty175_listen_t *listen, const int16_t *frame, size_t frame_samples)
{
    if (listen == NULL || frame == NULL || frame_samples == 0) {
        return false;
    }

    const uint32_t rms = frame_rms(frame, frame_samples);
    const uint32_t peak = frame_peak(frame, frame_samples);
    listen->last_rms = rms;
    listen->last_peak = peak;
    const bool was_active = listen->speech_active;
    waveform_push(listen, frame_wave_level(peak, rms), was_active);
    if (!listen->speech_active && listen->settle_frames > 0) {
        noise_floor_update(listen, rms, peak, true);
        listen->settle_frames--;
        listen->speech_frames = 0;
        return false;
    }

    const uint32_t rms_gate = listen->speech_active ? FACULTY175_LISTEN_RMS_END : adaptive_rms_start(listen);
    const uint32_t peak_gate = listen->speech_active ? FACULTY175_LISTEN_PEAK_END : adaptive_peak_start(listen);
    const bool voiced = rms >= rms_gate && peak >= peak_gate;

    if (!listen->speech_active) {
        if (voiced) {
            listen->speech_frames++;
            if (listen->speech_frames >= FACULTY175_LISTEN_START_FRAMES) {
                listen->speech_active = true;
                listen->silence_frames = 0;
                listen->capture_len_samples = 0;
                FACULTY175_LOG_STAGE(TAG, "capture", "VAD start — streaming rms=%u peak=%u",
                                     (unsigned)rms, (unsigned)peak);
                if (listen->stream_cb.on_speech_start != NULL) {
                    listen->stream_cb.on_speech_start(listen->stream_cb.ctx);
                }
            }
        } else {
            noise_floor_update(listen, rms, peak, false);
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
        if (listen->silence_frames >= FACULTY175_LISTEN_SILENCE_FRAMES) {
            const float dur_s = (float)listen->capture_len_samples / 16000.f;
            FACULTY175_LOG_STAGE(TAG, "capture", "VAD end — streamed %.2fs (%u samples)", dur_s,
                           (unsigned)listen->capture_len_samples);
            speech_segment_end(listen, true);
            return true;
        }
    }
    return false;
}
