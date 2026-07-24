#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Extract a bounded, prompt-safe reference block for one cacheable LunaSay
 * face from the current daily packet. Returns false on schema/date/face
 * mismatch, invalid bounds, or output truncation. Privacy-sensitive family
 * measurements are filtered from optional weather evidence. */
bool faculty175_lunasay_followup_extract(const char *json,
                                         size_t json_len,
                                         const char *expected_date,
                                         const char *face_slug,
                                         char *out,
                                         size_t out_cap);

#ifdef __cplusplus
}
#endif
