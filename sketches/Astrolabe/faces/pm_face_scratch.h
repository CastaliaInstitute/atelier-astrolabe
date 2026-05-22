#pragma once

#include <stddef.h>

/** Face-local temporary allocations. All blocks are released on face leave. */
void *pm_face_scratch_alloc(size_t bytes);
void pm_face_scratch_reset(void);
