#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "faculty175_faces.h"

#ifdef __cplusplus
extern "C" {
#endif

void faculty175_face_incidents_draw(uint32_t anim_ms);
bool faculty175_face_incidents_action(uint32_t seed_ms);
bool faculty175_face_incidents_scroll(int delta);
size_t faculty175_face_incidents_scroll_index(void);

#ifdef __cplusplus
}
#endif
