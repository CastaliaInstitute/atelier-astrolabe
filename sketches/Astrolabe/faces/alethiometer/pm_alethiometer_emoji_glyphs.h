// Auto-generated from the Google Fonts Noto Emoji monochrome font. Do not edit by hand.
// Source: https://github.com/googlefonts/noto-emoji and https://fonts.google.com/noto/specimen/Noto+Emoji.
// Font software license: SIL Open Font License 1.1.
#pragma once

#include <Arduino_GFX_Library.h>
#include <cstdint>

#define ALETHIOMETER_EMOJI_GLYPH_SIZE 20
#define ALETHIOMETER_EMOJI_GLYPH_COUNT 36

void pm_alethiometer_draw_emoji_glyph(Arduino_Canvas *gfx, int cx, int cy, int symbol_idx, uint16_t color);
const char *pm_alethiometer_emoji_codepoint(int symbol_idx);
