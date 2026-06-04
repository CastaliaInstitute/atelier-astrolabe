#pragma once

#include <stddef.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "paper_util.h"

/** Structured serial stages for WiFi / listen / STT / LLM / TTS tracing. */
#define PAPER_LOG_STAGE(tag, stage, fmt, ...) ESP_LOGI(tag, "[%s] " fmt, stage, ##__VA_ARGS__)
#define PAPER_LOG_STAGE_W(tag, stage, fmt, ...) ESP_LOGW(tag, "[%s] " fmt, stage, ##__VA_ARGS__)
#define PAPER_LOG_STAGE_E(tag, stage, fmt, ...) ESP_LOGE(tag, "[%s] " fmt, stage, ##__VA_ARGS__)

static inline uint32_t paper_log_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

static inline void paper_log_clip(char *dst, size_t cap, const char *src, size_t max_len)
{
    if (dst == NULL || cap == 0) {
        return;
    }
    if (src == NULL || src[0] == '\0') {
        dst[0] = '\0';
        return;
    }
    const size_t len = strlen(src);
    if (len <= max_len || max_len + 4 >= cap) {
        paper_strlcpy(dst, src, cap);
        return;
    }
    memcpy(dst, src, max_len);
    dst[max_len] = '.';
    dst[max_len + 1] = '.';
    dst[max_len + 2] = '.';
    dst[max_len + 3] = '\0';
}
