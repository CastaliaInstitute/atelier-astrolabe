#include "atom_listen.h"

#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"

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

esp_err_t atom_listen_init(atom_listen_t *listen)
{
    if (listen == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(listen, 0, sizeof(*listen));
    listen->utterance_queue = xQueueCreate(2, sizeof(atom_utterance_t));
    if (listen->utterance_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }
    listen->max_samples = ATOM_LISTEN_MAX_SECONDS * 16000;
    listen->min_samples = (ATOM_LISTEN_MIN_MS * 16000) / 1000;
    listen->capture_cap_samples = listen->max_samples;
    listen->capture_buf = heap_caps_malloc(listen->capture_cap_samples * sizeof(int16_t),
                                           MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (listen->capture_buf == NULL) {
        listen->capture_buf = malloc(listen->capture_cap_samples * sizeof(int16_t));
    }
    if (listen->capture_buf == NULL) {
        return ESP_ERR_NO_MEM;
    }
    atom_listen_reset(listen);
    ESP_LOGI(TAG, "listen ready max=%u samples", (unsigned)listen->max_samples);
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
}

static bool queue_utterance(atom_listen_t *listen)
{
    if (listen->capture_len_samples < listen->min_samples) {
        atom_listen_reset(listen);
        return false;
    }

    atom_utterance_t utterance = {
        .sample_count = listen->capture_len_samples,
    };
    const size_t bytes = utterance.sample_count * sizeof(int16_t);
    utterance.samples = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (utterance.samples == NULL) {
        utterance.samples = malloc(bytes);
    }
    if (utterance.samples == NULL) {
        atom_listen_reset(listen);
        return false;
    }
    memcpy(utterance.samples, listen->capture_buf, bytes);
    if (xQueueSend(listen->utterance_queue, &utterance, 0) != pdTRUE) {
        ESP_LOGW(TAG, "utterance queue full");
        free(utterance.samples);
        atom_listen_reset(listen);
        return false;
    }
    atom_listen_reset(listen);
    return true;
}

bool atom_listen_push_frame(atom_listen_t *listen, const int16_t *frame, size_t frame_samples)
{
    if (listen == NULL || frame == NULL || frame_samples == 0) {
        return false;
    }

    const uint32_t rms = frame_rms(frame, frame_samples);
    const bool voiced = rms >= (listen->speech_active ? ATOM_LISTEN_RMS_END : ATOM_LISTEN_RMS_START);

    if (!listen->speech_active) {
        if (voiced) {
            listen->speech_frames++;
            if (listen->speech_frames >= ATOM_LISTEN_START_FRAMES) {
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
        if (listen->silence_frames >= ATOM_LISTEN_SILENCE_FRAMES) {
            return queue_utterance(listen);
        }
    }
    return false;
}
