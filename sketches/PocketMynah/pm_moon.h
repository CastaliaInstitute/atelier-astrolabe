#pragma once

#include <Arduino_GFX_Library.h>

/** Draw lunar disk with embedded surface texture and phase terminator. */
void pm_moon_draw_disk(Arduino_Canvas *gfx, int cx, int cy, int r, float illum, bool waxing);
