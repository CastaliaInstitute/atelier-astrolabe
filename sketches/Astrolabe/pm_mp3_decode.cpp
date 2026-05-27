#include <stddef.h>

#include "esp_heap_caps.h"

static void *astrolabe_minimp3_scratch_alloc(size_t bytes) {
  static void *scratch = nullptr;
  if (!scratch) {
    scratch = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  }
  return scratch;
}

#define MINIMP3_SCRATCH_ALLOC astrolabe_minimp3_scratch_alloc
#define MINIMP3_IMPLEMENTATION
#include "minimp3_ex.h"
