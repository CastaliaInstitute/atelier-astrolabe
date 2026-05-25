#pragma once

#if defined(ASTROLABE_P4_TARGET)
#include_next "esp_heap_caps.h"
#else
#include <cstdlib>

#define MALLOC_CAP_SPIRAM 0
#define MALLOC_CAP_8BIT 0
#define MALLOC_CAP_DMA 0

static inline void *heap_caps_malloc(size_t size, int) { return std::malloc(size); }
static inline void *heap_caps_calloc(size_t n, size_t size, int) { return std::calloc(n, size); }
static inline void *heap_caps_aligned_alloc(size_t alignment, size_t size, int) {
  void *ptr = nullptr;
  return posix_memalign(&ptr, alignment, size) == 0 ? ptr : nullptr;
}
static inline void heap_caps_free(void *ptr) { std::free(ptr); }
#endif
