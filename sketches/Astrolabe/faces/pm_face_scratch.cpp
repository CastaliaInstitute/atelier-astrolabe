#include "faces/pm_face_scratch.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <stdlib.h>
#include <string.h>

#include "pm_heap.h"

namespace {

struct ScratchBlock {
  ScratchBlock *next;
  size_t bytes;
};

ScratchBlock *s_blocks = nullptr;
size_t s_live_bytes = 0;
size_t s_peak_bytes = 0;

}  // namespace

void *pm_face_scratch_alloc(size_t bytes) {
  if (bytes == 0) {
    return nullptr;
  }
  const size_t total = sizeof(ScratchBlock) + bytes;
  ScratchBlock *block =
      static_cast<ScratchBlock *>(heap_caps_malloc(total, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!block) {
    block = static_cast<ScratchBlock *>(heap_caps_malloc(total, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  }
  if (!block) {
    return nullptr;
  }
  block->next = s_blocks;
  block->bytes = bytes;
  s_blocks = block;
  s_live_bytes += bytes;
  if (s_live_bytes > s_peak_bytes) {
    s_peak_bytes = s_live_bytes;
  }
  void *ptr = block + 1;
  memset(ptr, 0, bytes);
  return ptr;
}

void pm_face_scratch_reset(void) {
  ScratchBlock *block = s_blocks;
  while (block) {
    ScratchBlock *next = block->next;
    heap_caps_free(block);
    block = next;
  }
  if (s_live_bytes || s_peak_bytes) {
    Serial.printf("face_scratch: reset live=%u peak=%u\n", static_cast<unsigned>(s_live_bytes),
                  static_cast<unsigned>(s_peak_bytes));
  }
  s_blocks = nullptr;
  s_live_bytes = 0;
  s_peak_bytes = 0;
}
