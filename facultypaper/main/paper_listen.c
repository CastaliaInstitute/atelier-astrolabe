#include "paper_listen.h"

#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"

#include "paper_log.h"

static const char *TAG = "paper_listen";

static uint32_t isqrt_u64(uint64_t value)
{
    uint64_t bit = 1ULL << 62;
    while (bit > value) {
        bit >>= 2;
    }
    uint64_t result = 0;
    while (bit != 0) {
        if (value >= result + bit) {
            value -= result + bit;
            result = (result >> 1) + bit;
        } else {
            result >>= 1;
        }
        bit >>= 2;
    }
    return (uint32_t)result;
}

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
    return isqrt_u64(acc / count);
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

static void waveform_push(paper_listen_t *listen, uint8_t level)
{
    if (listen == NULL) {
        return;
    }
    listen->waveform[listen->waveform_head] = level;
    listen->waveform_head = (uint16_t)((listen->waveform_head + 1) % PAPER_LISTEN_WAVEFORM_LEN);
}

esp_err_t paper_listen_init(paper_listen_t *listen)
{
    if (listen == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(listen, 0, sizeof(*listen));
    listen->utterance_queue = xQueueCreate(2, sizeof(paper_utterance_t));
    if (listen->utterance_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }
    listen->max_samples = PAPER_LISTEN_MAX_SECONDS * 16000;
    listen->min_samples = (PAPER_LISTEN_MIN_MS * 16000) / 1000;
    listen->capture_cap_samples = listen->max_samples;
    listen->waveform_head = 0;
    memset(listen->waveform, 0, sizeof(listen->waveform));
    listen->capture_buf = heap_caps_malloc(listen->capture_cap_samples * sizeof(int16_t),
                                           MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (listen->capture_buf == NULL) {
        listen->capture_buf = malloc(listen->capture_cap_samples * sizeof(int16_t));
    }
    if (listen->capture_buf == NULL) {
        return ESP_ERR_NO_MEM;
    }
    paper_listen_reset(listen);
    ESP_LOGI(TAG, "listen ready max=%u samples", (unsigned)listen->max_samples);
    return ESP_OK;
}

void paper_listen_reset(paper_listen_t *listen)
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

uint8_t paper_listen_meter_level(const paper_listen_t *listen)
{
    if (listen == NULL || listen->utterance_queue == NULL || listen->last_rms == 0) {
        return 0;
    }
    const uint32_t floor = listen->speech_active ? PAPER_LISTEN_RMS_END : 80;
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

bool paper_listen_speech_active(const paper_listen_t *listen)
{
    return listen != NULL && listen->speech_active;
}

uint32_t paper_listen_last_rms(const paper_listen_t *listen)
{
    if (listen == NULL) {
        return 0;
    }
    return listen->last_rms;
}

void paper_listen_waveform_copy(const paper_listen_t *listen, uint8_t *out, size_t len)
{
    if (listen == NULL || out == NULL || len == 0) {
        return;
    }
    const size_t n = len < PAPER_LISTEN_WAVEFORM_LEN ? len : PAPER_LISTEN_WAVEFORM_LEN;
    const uint16_t head = listen->waveform_head;
    for (size_t i = 0; i < n; ++i) {
        out[i] = listen->waveform[(head + i) % PAPER_LISTEN_WAVEFORM_LEN];
    }
    if (len > n) {
        memset(out + n, 0, len - n);
    }
}

static bool queue_utterance(paper_listen_t *listen)
{
    if (listen->capture_len_samples < listen->min_samples) {
        paper_listen_reset(listen);
        return false;
    }

    paper_utterance_t utterance = {
        .sample_count = listen->capture_len_samples,
    };
    const size_t bytes = utterance.sample_count * sizeof(int16_t);
    utterance.samples = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (utterance.samples == NULL) {
        utterance.samples = malloc(bytes);
    }
    if (utterance.samples == NULL) {
        paper_listen_reset(listen);
        return false;
    }
    memcpy(utterance.samples, listen->capture_buf, bytes);
    if (xQueueSend(listen->utterance_queue, &utterance, 0) != pdTRUE) {
        PAPER_LOG_STAGE_W(TAG, "capture", "utterance queue full - dropped");
        free(utterance.samples);
        paper_listen_reset(listen);
        return false;
    }
    const float dur_s = (float)utterance.sample_count / 16000.f;
    PAPER_LOG_STAGE(TAG, "capture", "utterance queued %.2fs (%u samples)", dur_s, (unsigned)utterance.sample_count);
    paper_listen_reset(listen);
    return true;
}

bool paper_listen_push_frame(paper_listen_t *listen, const int16_t *frame, size_t frame_samples)
{
    if (listen == NULL || frame == NULL || frame_samples == 0) {
        return false;
    }

    const uint32_t rms = frame_rms(frame, frame_samples);
    listen->last_rms = rms;
    waveform_push(listen, frame_wave_level(frame, frame_samples, rms));
    const bool voiced = rms >= (listen->speech_active ? PAPER_LISTEN_RMS_END : PAPER_LISTEN_RMS_START);

    if (!listen->speech_active) {
        if (voiced) {
            listen->speech_frames++;
            if (listen->speech_frames >= PAPER_LISTEN_START_FRAMES) {
                listen->speech_active = true;
                listen->silence_frames = 0;
                listen->capture_len_samples = 0;
            }
        } else {
            listen->speech_frames = 0;
        }
        return false;
    }

    if (listen->capture_len_samples + frame_samples > listen->capture_cap_samples) {
        ESP_LOGW(TAG, "utterance max length reached");
        return queue_utterance(listen);
    }
    memcpy(listen->capture_buf + listen->capture_len_samples, frame, frame_samples * sizeof(int16_t));
    listen->capture_len_samples += frame_samples;

    if (voiced) {
        listen->silence_frames = 0;
    } else {
        listen->silence_frames++;
        if (listen->silence_frames >= PAPER_LISTEN_SILENCE_FRAMES) {
            return queue_utterance(listen);
        }
    }
    return false;
}
