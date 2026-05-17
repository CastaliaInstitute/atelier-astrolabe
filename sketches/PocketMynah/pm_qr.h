#pragma once

#include <stdbool.h>

class Arduino_Canvas;

/** Draw a QR code for `url` centered at (cx, cy), max pixel width `max_px`. */
bool pm_qr_draw_url(Arduino_Canvas *gfx, const char *url, int cx, int cy, int max_px);

/** Drop cached encode state (e.g. when URL changes). */
void pm_qr_invalidate_cache(void);
