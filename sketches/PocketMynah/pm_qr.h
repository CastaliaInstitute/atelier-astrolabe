#pragma once

#include <Arduino.h>

class Arduino_Canvas;

/** Draw a QR for `text` centered at (cx, cy), bounding box width `max_px`. */
bool pm_qr_draw(Arduino_Canvas *gfx, int cx, int cy, int max_px, const char *text);
