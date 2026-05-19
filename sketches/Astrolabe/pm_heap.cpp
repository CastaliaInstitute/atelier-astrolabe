#include "pm_heap.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <stdlib.h>

#include "pm_config.h"

static const char *TAG = "pm_heap";

static constexpr uint32_t kInternalCaps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;

uint32_t pm_heap_internal_free(void) {
  return static_cast<uint32_t>(heap_caps_get_free_size(kInternalCaps));
}

uint32_t pm_heap_internal_largest(void) {
  return static_cast<uint32_t>(heap_caps_get_largest_free_block(kInternalCaps));
}

uint32_t pm_heap_psram_free(void) {
  return static_cast<uint32_t>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
}

bool pm_heap_tls_ready(uint32_t min_free, const char *tag) {
  const uint32_t free_i = pm_heap_internal_free();
  const uint32_t largest_i = pm_heap_internal_largest();
  if (free_i < min_free || largest_i < MYNAH_TLS_MIN_LARGEST_INTERNAL) {
    ESP_LOGW(TAG, "%s low heap: internal=%u largest=%u psram=%u need=%u/%u",
             tag ? tag : "tls", static_cast<unsigned>(free_i), static_cast<unsigned>(largest_i),
             static_cast<unsigned>(pm_heap_psram_free()), static_cast<unsigned>(min_free),
             static_cast<unsigned>(MYNAH_TLS_MIN_LARGEST_INTERNAL));
    return false;
  }
  return true;
}

void *pm_heap_alloc_response(size_t bytes) {
  if (bytes == 0) {
    return nullptr;
  }
  void *p = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (p) {
    return p;
  }
  const uint32_t free_i = pm_heap_internal_free();
  const uint32_t largest_i = pm_heap_internal_largest();
  if (bytes > largest_i || free_i < bytes || free_i - static_cast<uint32_t>(bytes) < MYNAH_RESPONSE_INTERNAL_FLOOR) {
    ESP_LOGW(TAG, "response alloc refused: bytes=%u internal=%u largest=%u floor=%u",
             static_cast<unsigned>(bytes), static_cast<unsigned>(free_i), static_cast<unsigned>(largest_i),
             static_cast<unsigned>(MYNAH_RESPONSE_INTERNAL_FLOOR));
    return nullptr;
  }
  return malloc(bytes);
}

void pm_heap_log(const char *tag) {
  Serial.printf("heap: %s internal=%u largest=%u psram=%u\n", tag ? tag : "-",
                static_cast<unsigned>(pm_heap_internal_free()), static_cast<unsigned>(pm_heap_internal_largest()),
                static_cast<unsigned>(pm_heap_psram_free()));
}
