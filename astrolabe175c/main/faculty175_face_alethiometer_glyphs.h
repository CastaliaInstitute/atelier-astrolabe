#pragma once

#include <stdint.h>

#define FACULTY175_ALETHIOMETER_GLYPH_SIZE 20
#define FACULTY175_ALETHIOMETER_GLYPH_COUNT 36

void faculty175_face_alethiometer_draw_glyph(int cx, int cy, int symbol_idx, uint16_t color, uint8_t scale);
const char *faculty175_face_alethiometer_glyph_codepoint(int symbol_idx);
