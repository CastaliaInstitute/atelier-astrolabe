#pragma once

#include <stddef.h>
#include <stdint.h>

uint32_t pm_heap_internal_free(void);
uint32_t pm_heap_internal_largest(void);
uint32_t pm_heap_psram_free(void);

/** True when internal heap is healthy enough to start a TLS/HTTP transaction. */
bool pm_heap_tls_ready(uint32_t min_free, const char *tag);

/** Prefer PSRAM; only fall back to internal RAM when the requested reserve remains. */
void *pm_heap_alloc_response(size_t bytes);

void pm_heap_log(const char *tag);
