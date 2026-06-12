#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void faculty175_face_alethiometer_draw(uint32_t anim_ms);
void faculty175_face_alethiometer_cast(uint32_t seed);
bool faculty175_face_alethiometer_apply_reply(const char *question, const char *reply_json);
bool faculty175_face_alethiometer_current(int out_targets[4]);
bool faculty175_face_alethiometer_context(int out_targets[4],
                                          char *question,
                                          size_t question_cap,
                                          char *spoken,
                                          size_t spoken_cap);
const char *faculty175_face_alethiometer_symbol_name(int idx);
