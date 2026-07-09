#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "faculty175_faces.h"

bool faculty175_face_dispatch_draw(faculty175_face_id_t id, uint32_t anim_ms);
bool faculty175_face_dispatch_action(faculty175_face_id_t id, uint32_t seed_ms);
