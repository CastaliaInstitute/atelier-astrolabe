#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

void faculty175_face_runes_draw(uint32_t anim_ms);
void faculty175_face_runes_init(void);
void faculty175_face_runes_cast(void);
esp_err_t faculty175_face_runes_save(void);
bool faculty175_face_runes_current(int out_spread[3]);
const char *faculty175_face_rune_name(int idx);
const char *faculty175_face_rune_keyword(int idx);
const char *faculty175_face_rune_slot(int idx);
