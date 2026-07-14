#pragma once

#include <stddef.h>

#define MALLOC_CAP_SPIRAM 0x01
#define MALLOC_CAP_8BIT 0x02
#define MALLOC_CAP_INTERNAL 0x04

void *heap_caps_malloc(size_t size, unsigned caps);
void *heap_caps_realloc(void *ptr, size_t size, unsigned caps);
void heap_caps_free(void *ptr);
size_t heap_caps_get_free_size(unsigned caps);
