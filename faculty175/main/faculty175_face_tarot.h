#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void faculty175_face_tarot_draw(uint32_t anim_ms);
void faculty175_face_tarot_draw_card(uint32_t seed_ms);
int faculty175_face_tarot_current_card(void);

#ifdef __cplusplus
}
#endif
