#pragma once

#include <stdint.h>

#include "faculty175_board.h"

void faculty175_face_theritor_draw(uint32_t anim_ms);
void faculty175_face_theritor_set_state(faculty175_ui_state_t state);
void faculty175_face_theritor_set_context(const char *respondent, const char *mode);
void faculty175_face_theritor_set_reply(const char *reply);
