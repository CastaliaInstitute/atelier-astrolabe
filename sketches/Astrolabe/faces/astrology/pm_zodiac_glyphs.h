#pragma once

#include <stdint.h>

class Arduino_Canvas;

/** Draw one tropical sign icon centered at (cx, cy). [highlight] adds a soft halo. */
void pm_zodiac_draw_glyph(Arduino_Canvas *gfx, int cx, int cy, int sign_idx, uint16_t color, bool highlight);

/** Place glyph on a ring: center at polar (rcx, rcy, r, ang). */
void pm_zodiac_draw_at_polar(Arduino_Canvas *gfx, int rcx, int rcy, int r, float ang, int sign_idx,
                             uint16_t color, bool highlight);

/** All twelve sign icons on a ring (call after rainbow rim so labels stay visible). */
void pm_zodiac_draw_sign_ring(Arduino_Canvas *gfx, int cx, int cy, int r_lab, int highlight_sign,
                              uint16_t normal_color);

/** Draw one planet/body icon (body_idx = PmBodyId). */
void pm_planet_draw_glyph(Arduino_Canvas *gfx, int cx, int cy, int body_idx, uint16_t color, bool highlight);

/** Place planet glyph on a ring at polar angle. */
void pm_planet_draw_at_polar(Arduino_Canvas *gfx, int rcx, int rcy, int r, float ang, int body_idx,
                             uint16_t color, bool highlight);
