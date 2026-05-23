#include "pm_zodiac_glyphs.h"

#include <Arduino_GFX_Library.h>
#include <math.h>
#include <pgmspace.h>

#include "planet_glyphs.h"
#include "pm_display.h"
#include "zodiac_glyphs.h"

static uint16_t blend565(uint16_t fg, uint16_t bg, float a) {
  if (a <= 0.f) {
    return bg;
  }
  if (a >= 1.f) {
    return fg;
  }
  const float ia = 1.f - a;
  const uint8_t fr = ((fg >> 11) & 0x1F) * 255 / 31;
  const uint8_t fg8 = ((fg >> 5) & 0x3F) * 255 / 63;
  const uint8_t fb = (fg & 0x1F) * 255 / 31;
  const uint8_t br = ((bg >> 11) & 0x1F) * 255 / 31;
  const uint8_t bg8 = ((bg >> 5) & 0x3F) * 255 / 63;
  const uint8_t bb = (bg & 0x1F) * 255 / 31;
  const uint8_t r = static_cast<uint8_t>(fr * a + br * ia);
  const uint8_t g = static_cast<uint8_t>(fg8 * a + bg8 * ia);
  const uint8_t b = static_cast<uint8_t>(fb * a + bb * ia);
  return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

static uint16_t sample_bg(PmDisplayCanvas *gfx, int x, int y) {
  uint16_t *fb = gfx->getFramebuffer();
  if (!fb) {
    return 0;
  }
  const int w = gfx->width();
  const int h = gfx->height();
  if (x < 0 || y < 0 || x >= w || y >= h) {
    return 0;
  }
  return fb[y * w + x];
}

static void pm_glyph_draw_alpha(PmDisplayCanvas *gfx, int cx, int cy, const uint8_t *alpha_prog, int S,
                                uint16_t color, bool highlight) {
  if (!gfx || !alpha_prog || S <= 0) {
    return;
  }

  const int x0 = cx - S / 2;
  const int y0 = cy - S / 2;
  const int scr_w = gfx->width();
  const int scr_h = gfx->height();
  uint16_t *fb = gfx->getFramebuffer();

  const uint16_t halo = highlight ? gfx->color565(
                                        static_cast<uint8_t>(((color >> 11) & 0x1F) * 255 / 31 * 0.5f),
                                        static_cast<uint8_t>(((color >> 5) & 0x3F) * 255 / 63 * 0.5f),
                                        static_cast<uint8_t>((color & 0x1F) * 255 / 31 * 0.5f))
                                  : 0;

  for (int dy = 0; dy < S; ++dy) {
    const int py = y0 + dy;
    if (py < 0 || py >= scr_h) {
      continue;
    }
    for (int dx = 0; dx < S; ++dx) {
      const int px = x0 + dx;
      if (px < 0 || px >= scr_w) {
        continue;
      }
      const int idx = dy * S + dx;
      const uint8_t a = pgm_read_byte(alpha_prog + idx);
      if (a == 0) {
        continue;
      }
      const uint16_t bg = fb ? fb[py * scr_w + px] : sample_bg(gfx, px, py);
      uint16_t out = blend565(color, bg, static_cast<float>(a) / 255.f);
      if (highlight && a > 32) {
        out = blend565(halo, out, static_cast<float>(a) / 255.f * 0.4f);
      }
      if (fb) {
        fb[py * scr_w + px] = out;
      } else {
        gfx->drawPixel(px, py, out);
      }
    }
  }
}

void pm_zodiac_draw_glyph(PmDisplayCanvas *gfx, int cx, int cy, int sign_idx, uint16_t color, bool highlight) {
  if (sign_idx < 0 || sign_idx >= ZODIAC_GLYPH_COUNT) {
    return;
  }
  pm_glyph_draw_alpha(gfx, cx, cy, kZodiacGlyphAlpha[sign_idx], ZODIAC_GLYPH_SIZE, color, highlight);
}

void pm_planet_draw_glyph(PmDisplayCanvas *gfx, int cx, int cy, int body_idx, uint16_t color, bool highlight) {
  if (body_idx < 0 || body_idx >= PLANET_GLYPH_COUNT) {
    return;
  }
  pm_glyph_draw_alpha(gfx, cx, cy, kPlanetGlyphAlpha[body_idx], PLANET_GLYPH_SIZE, color, highlight);
}

void pm_planet_draw_at_polar(PmDisplayCanvas *gfx, int rcx, int rcy, int r, float ang, int body_idx,
                             uint16_t color, bool highlight) {
  const int tx = rcx + static_cast<int>(lrintf(cosf(ang) * static_cast<float>(r)));
  const int ty = rcy + static_cast<int>(lrintf(sinf(ang) * static_cast<float>(r)));
  pm_planet_draw_glyph(gfx, tx, ty, body_idx, color, highlight);
}

void pm_zodiac_draw_at_polar(PmDisplayCanvas *gfx, int rcx, int rcy, int r, float ang, int sign_idx,
                             uint16_t color, bool highlight) {
  const int tx = rcx + static_cast<int>(lrintf(cosf(ang) * static_cast<float>(r)));
  const int ty = rcy + static_cast<int>(lrintf(sinf(ang) * static_cast<float>(r)));
  pm_zodiac_draw_glyph(gfx, tx, ty, sign_idx, color, highlight);
}

void pm_zodiac_draw_sign_ring(PmDisplayCanvas *gfx, int cx, int cy, int r_lab, int highlight_sign,
                              uint16_t normal_color) {
  if (!gfx) {
    return;
  }
  constexpr float kTwoPi = 6.283185307179586f;
  constexpr float kPi = 3.141592653589793f;
  for (int s = 0; s < 12; ++s) {
    const float amid = (static_cast<float>(s) + 0.5f) * (kTwoPi / 12.f) - kPi * 0.5f;
    const bool hi = (highlight_sign == s);
    const uint16_t col = hi ? gfx->color565(255, 252, 220) : normal_color;
    pm_zodiac_draw_at_polar(gfx, cx, cy, r_lab, amid, s, col, hi);
  }
}
