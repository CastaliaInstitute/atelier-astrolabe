#include "pm_moon.h"

#include <pgmspace.h>

#include "pm_moon_tex.h"

static uint8_t moon_tex_sample_gray(int u, int v) {
  const int ts = kPmMoonTexSize;
  if (u < 0) {
    u = 0;
  } else if (u >= ts) {
    u = ts - 1;
  }
  if (v < 0) {
    v = 0;
  } else if (v >= ts) {
    v = ts - 1;
  }
  return pgm_read_byte(kPmMoonTexGray + (size_t)v * (size_t)ts + (size_t)u);
}

static uint16_t moon_gray_to_rgb565(Arduino_Canvas *gfx, uint8_t g) {
  return gfx->color565(g, g, g);
}

static uint16_t moon_darken_gray(Arduino_Canvas *gfx, uint8_t g) {
  const uint8_t dr = 28;
  const uint8_t dg = 32;
  const uint8_t db = 44;
  const uint8_t ng = static_cast<uint8_t>((g * 22 + dg * 78) / 100);
  const uint8_t nr = static_cast<uint8_t>((g * 22 + dr * 78) / 100);
  const uint8_t nb = static_cast<uint8_t>((g * 22 + db * 78) / 100);
  return gfx->color565(nr, ng, nb);
}

void pm_moon_draw_disk(Arduino_Canvas *gfx, int cx, int cy, int r, float illum, bool waxing) {
  if (!gfx || r < 4) {
    return;
  }
  const int ts = kPmMoonTexSize;
  const uint16_t c_ring = gfx->color565(88, 98, 118);
  const float t = (1.f - 2.f * illum) * static_cast<float>(r);
  const int r2 = r * r;
  const int denom = r * 2;

  for (int dy = -r; dy <= r; ++dy) {
    for (int dx = -r; dx <= r; ++dx) {
      const int d2 = dx * dx + dy * dy;
      if (d2 > r2) {
        continue;
      }
      const int tu = ((dx + r) * (ts - 1)) / denom;
      const int tv = ((dy + r) * (ts - 1)) / denom;
      const uint8_t gray = moon_tex_sample_gray(tu, tv);
      if (gray == 0) {
        continue;
      }
      const bool lit = waxing ? (static_cast<float>(dx) > t) : (static_cast<float>(dx) < t);
      const uint16_t col = lit ? moon_gray_to_rgb565(gfx, gray) : moon_darken_gray(gfx, gray);
      gfx->drawPixel(cx + dx, cy + dy, col);
    }
  }
  gfx->drawCircle(cx, cy, r, c_ring);
}
