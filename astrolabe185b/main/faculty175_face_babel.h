#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void faculty175_face_babel_draw(uint32_t anim_ms);
bool faculty175_face_babel_update_from_transcript(const char *transcript);
void faculty175_face_babel_set_reply(const char *reply);
bool faculty175_face_babel_active(void);

#ifdef __cplusplus
}
#endif
