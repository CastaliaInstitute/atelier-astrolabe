#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "faculty175_faces.h"

#ifdef __cplusplus
extern "C" {
#endif

void faculty175_face_wifilab_draw(faculty175_face_id_t id, uint32_t anim_ms);
bool faculty175_face_wifilab_action(faculty175_face_id_t id, uint32_t seed_ms);
void faculty175_face_wifilab_tick(faculty175_face_id_t id, uint32_t now_ms);
void faculty175_face_wifilab_enter(faculty175_face_id_t id);
void faculty175_face_wifilab_leave(faculty175_face_id_t id);

#ifdef __cplusplus
}
#endif
