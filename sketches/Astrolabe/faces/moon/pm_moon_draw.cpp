#include "pm_moon_draw.h"

#include <Arduino_GFX_Library.h>
#include <math.h>
#include <pgmspace.h>

#include "moon_texture.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

bool pm_moon_phase_from_transit(const PmTransitPositions *tp, float *illum, bool *waxing,
                                double *elong_deg) {
  if (!tp || !tp->ok || !illum || !waxing) {
    return false;
  }
  double el = tp->lon[kPmBodyMoon] - tp->lon[kPmBodySun];
  while (el < 0.0) {
    el += 360.0;
  }
  while (el >= 360.0) {
    el -= 360.0;
  }
  *waxing = el < 180.0;
  const float rad = static_cast<float>(el * (M_PI / 180.0));
  *illum = (1.f - cosf(rad)) * 0.5f;
  if (elong_deg) {
    *elong_deg = el;
  }
  return true;
}

bool pm_moon_point_lit(int dx, int screen_r, float illum, bool waxing) {
  return pm_moon_point_lit_curved(dx, 0, screen_r, illum, waxing);
}

bool pm_moon_point_lit_curved(int dx, int dy, int screen_r, float illum, bool waxing) {
  if (screen_r <= 0) {
    return false;
  }
  const int r2 = screen_r * screen_r;
  const int y2 = dy * dy;
  if (dx * dx + y2 > r2) {
    return false;
  }
  const float limb_x = sqrtf(static_cast<float>(r2 - y2));
  const float terminator_x = (1.f - 2.f * illum) * limb_x;
  const float x = static_cast<float>(dx);
  return waxing ? (x >= terminator_x) : (x <= -terminator_x);
}

static uint16_t gray_to_565(Arduino_GFX *gfx, uint8_t g) {
  return gfx->color565(g, g, g);
}

/** Blend greyscale in 8-bit space once — avoids RGB565's extra green bits tinting the moon. */
static uint8_t moon_shaded_luma(uint8_t tex_g, float shade) {
  if (shade <= 0.f) {
    return 0;
  }
  if (shade >= 0.99f) {
    return tex_g;
  }
  const float dark = static_cast<float>(tex_g) * 0.07f;
  const float lit = static_cast<float>(tex_g);
  return static_cast<uint8_t>(lrintf(dark + (lit - dark) * shade));
}

static float moon_pixel_shade(int dx, int dy, int r, float illum, bool waxing) {
  if (dx * dx + dy * dy > r * r) {
    return -1.f;
  }
  if (!pm_moon_point_lit_curved(dx, dy, r, illum, waxing)) {
    return 0.07f;
  }
  constexpr float k_edge = 1.25f;
  const float limb_x = sqrtf(static_cast<float>(r * r - dy * dy));
  const float terminator_x = (1.f - 2.f * illum) * limb_x;
  const float d = waxing ? (static_cast<float>(dx) - terminator_x)
                         : (-terminator_x - static_cast<float>(dx));
  if (d >= k_edge) {
    return 1.f;
  }
  if (d <= -k_edge) {
    return 0.92f;
  }
  const float u = (d + k_edge) / (2.f * k_edge);
  return 0.92f + 0.08f * u;
}

static uint8_t sample_moon_tex_grey(int tx, int ty) {
  if (tx < 0 || ty < 0 || tx >= MOON_TEX_SIZE || ty >= MOON_TEX_SIZE) {
    return 0;
  }
  return pgm_read_byte(&kMoonTextureGray[ty * MOON_TEX_SIZE + tx]);
}

void pm_moon_draw_disk(Arduino_Canvas *gfx, int cx, int cy, int screen_r, float illum, bool waxing,
                       bool show_rim) {
  if (!gfx || screen_r <= 0) {
    return;
  }
  const int tex_r = MOON_TEX_SIZE / 2 - 1;
  const int tex_c = MOON_TEX_SIZE / 2;
  const int r2 = screen_r * screen_r;

  for (int dy = -screen_r; dy <= screen_r; ++dy) {
    for (int dx = -screen_r; dx <= screen_r; ++dx) {
      if (dx * dx + dy * dy > r2) {
        continue;
      }
      const int tx = tex_c + (dx * tex_r) / screen_r;
      const int ty = tex_c + (dy * tex_r) / screen_r;
      const uint8_t tex_g = sample_moon_tex_grey(tx, ty);
      if (tex_g == 0) {
        continue;
      }
      const float shade = moon_pixel_shade(dx, dy, screen_r, illum, waxing);
      if (shade < 0.f) {
        continue;
      }
      const uint8_t lum = moon_shaded_luma(tex_g, shade);
      gfx->drawPixel(cx + dx, cy + dy, gray_to_565(gfx, lum));
    }
  }
  if (show_rim) {
    const uint16_t rim = gfx->color565(88, 98, 118);
    gfx->drawCircle(cx, cy, screen_r, rim);
  }
}
