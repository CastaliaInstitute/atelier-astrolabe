#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "faculty175_face_tarot_assets.h"

#ifdef __cplusplus
extern "C" {
#endif

bool faculty175_tarot_spiffs_image_get(int idx,
                                       const faculty175_tarot_card_t *card,
                                       const uint16_t **pixels,
                                       int *w,
                                       int *h);
const char *faculty175_tarot_spiffs_image_error(void);

#ifdef __cplusplus
}
#endif
