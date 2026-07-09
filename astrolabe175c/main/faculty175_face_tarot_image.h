#pragma once

#include <stdbool.h>

#include "faculty175_face_tarot_assets.h"

#ifdef __cplusplus
extern "C" {
#endif

void faculty175_tarot_image_request(int idx, const faculty175_tarot_card_t *card);
bool faculty175_tarot_image_draw_cached(int idx);
bool faculty175_tarot_image_busy(void);
const char *faculty175_tarot_image_error(void);

#ifdef __cplusplus
}
#endif
