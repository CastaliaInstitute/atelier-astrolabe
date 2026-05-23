#pragma once

#include <stdint.h>

class PmDisplayCanvas;

/** Draw embedded yantra glyph for chakra index 0..6 (root → crown). */
void pm_chakra_draw_glyph(PmDisplayCanvas *gfx, int cx, int cy, int chakra_idx, uint16_t color, bool highlight);
