#include "pm_chakra_glyphs.h"

#include <Arduino_GFX_Library.h>
#include <pgmspace.h>

#include "chakra_glyphs.h"

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

void pm_chakra_draw_glyph(Arduino_Canvas *gfx, int cx, int cy, int chakra_idx, uint16_t color, bool highlight) {
  if (!gfx || chakra_idx < 0 || chakra_idx >= CHAKRA_GLYPH_COUNT) {
    return;
  }

  const int S = CHAKRA_GLYPH_SIZE;
  const uint8_t *alpha_prog = kChakraGlyphAlpha[chakra_idx];
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
      const uint16_t bg = fb ? fb[py * scr_w + px] : 0;
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
