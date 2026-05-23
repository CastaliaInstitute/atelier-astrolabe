#include "Arduino_GFX_Library.h"

#include <emscripten.h>

#include <cctype>
#include <cstring>

SerialClass Serial;

uint32_t millis(void) { return static_cast<uint32_t>(emscripten_get_now()); }
uint32_t micros(void) { return static_cast<uint32_t>(emscripten_get_now() * 1000.0); }
void delay(uint32_t) {}
uint32_t esp_random(void) {
  static uint32_t s = 0x12345678u;
  s ^= s << 13;
  s ^= s >> 17;
  s ^= s << 5;
  return s;
}

void Arduino_GFX::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
  if (w < 0) {
    x += w;
    w = -w;
  }
  if (h < 0) {
    y += h;
    h = -h;
  }
  if (x < 0) {
    w += x;
    x = 0;
  }
  if (y < 0) {
    h += y;
    y = 0;
  }
  if (x + w > _width) w = _width - x;
  if (y + h > _height) h = _height - y;
  if (w <= 0 || h <= 0) return;
  writeFillRectPreclipped(x, y, w, h, color);
}

void Arduino_GFX::drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
  drawFastHLine(x, y, w, color);
  drawFastHLine(x, y + h - 1, w, color);
  drawFastVLine(x, y, h, color);
  drawFastVLine(x + w - 1, y, h, color);
}

void Arduino_GFX::drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color) {
  int16_t dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  int16_t dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
  int16_t err = dx + dy;
  for (;;) {
    writePixel(x0, y0, color);
    if (x0 == x1 && y0 == y1) break;
    int16_t e2 = 2 * err;
    if (e2 >= dy) {
      err += dy;
      x0 += sx;
    }
    if (e2 <= dx) {
      err += dx;
      y0 += sy;
    }
  }
}

void Arduino_GFX::drawCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color) {
  int16_t f = 1 - r, ddF_x = 1, ddF_y = -2 * r, x = 0, y = r;
  writePixel(x0, y0 + r, color); writePixel(x0, y0 - r, color);
  writePixel(x0 + r, y0, color); writePixel(x0 - r, y0, color);
  while (x < y) {
    if (f >= 0) { y--; ddF_y += 2; f += ddF_y; }
    x++; ddF_x += 2; f += ddF_x;
    writePixel(x0 + x, y0 + y, color); writePixel(x0 - x, y0 + y, color);
    writePixel(x0 + x, y0 - y, color); writePixel(x0 - x, y0 - y, color);
    writePixel(x0 + y, y0 + x, color); writePixel(x0 - y, y0 + x, color);
    writePixel(x0 + y, y0 - x, color); writePixel(x0 - y, y0 - x, color);
  }
}

void Arduino_GFX::fillCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color) {
  for (int16_t y = -r; y <= r; ++y) {
    int16_t half = static_cast<int16_t>(std::sqrt(static_cast<float>(r * r - y * y)));
    drawFastHLine(x0 - half, y0 + y, 2 * half + 1, color);
  }
}

void Arduino_GFX::drawEllipse(int16_t x0, int16_t y0, int16_t rx, int16_t ry, uint16_t color) {
  for (int deg = 0; deg < 360; ++deg) {
    const float a = deg * 0.01745329252f;
    writePixel(x0 + static_cast<int16_t>(std::cos(a) * rx), y0 + static_cast<int16_t>(std::sin(a) * ry), color);
  }
}

void Arduino_GFX::fillEllipse(int16_t x0, int16_t y0, int16_t rx, int16_t ry, uint16_t color) {
  for (int16_t y = -ry; y <= ry; ++y) {
    float yy = static_cast<float>(y) / static_cast<float>(ry);
    int16_t half = static_cast<int16_t>(std::sqrt(std::max(0.0f, 1.0f - yy * yy)) * rx);
    drawFastHLine(x0 - half, y0 + y, 2 * half + 1, color);
  }
}

void Arduino_GFX::drawTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2,
                               uint16_t color) {
  drawLine(x0, y0, x1, y1, color);
  drawLine(x1, y1, x2, y2, color);
  drawLine(x2, y2, x0, y0, color);
}

void Arduino_GFX::fillTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2,
                               uint16_t color) {
  int16_t min_y = std::min({y0, y1, y2});
  int16_t max_y = std::max({y0, y1, y2});
  for (int16_t y = min_y; y <= max_y; ++y) {
    int16_t xs[3];
    int n = 0;
    auto edge = [&](int16_t ax, int16_t ay, int16_t bx, int16_t by) {
      if ((ay <= y && by > y) || (by <= y && ay > y)) xs[n++] = ax + (y - ay) * (bx - ax) / (by - ay);
    };
    edge(x0, y0, x1, y1); edge(x1, y1, x2, y2); edge(x2, y2, x0, y0);
    if (n >= 2) {
      if (xs[0] > xs[1]) std::swap(xs[0], xs[1]);
      drawFastHLine(xs[0], y, xs[1] - xs[0] + 1, color);
    }
  }
}

void Arduino_GFX::drawRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t, uint16_t color) {
  drawRect(x, y, w, h, color);
}

void Arduino_GFX::fillRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t, uint16_t color) {
  fillRect(x, y, w, h, color);
}

namespace {

const uint8_t *default_glyph(char c) {
  static constexpr uint8_t kBlank[5] = {0, 0, 0, 0, 0};
  static constexpr uint8_t kDigits[10][5] = {
      {0x3e, 0x51, 0x49, 0x45, 0x3e}, {0x00, 0x42, 0x7f, 0x40, 0x00}, {0x42, 0x61, 0x51, 0x49, 0x46},
      {0x21, 0x41, 0x45, 0x4b, 0x31}, {0x18, 0x14, 0x12, 0x7f, 0x10}, {0x27, 0x45, 0x45, 0x45, 0x39},
      {0x3c, 0x4a, 0x49, 0x49, 0x30}, {0x01, 0x71, 0x09, 0x05, 0x03}, {0x36, 0x49, 0x49, 0x49, 0x36},
      {0x06, 0x49, 0x49, 0x29, 0x1e},
  };
  static constexpr uint8_t kLetters[26][5] = {
      {0x7e, 0x11, 0x11, 0x11, 0x7e}, {0x7f, 0x49, 0x49, 0x49, 0x36}, {0x3e, 0x41, 0x41, 0x41, 0x22},
      {0x7f, 0x41, 0x41, 0x22, 0x1c}, {0x7f, 0x49, 0x49, 0x49, 0x41}, {0x7f, 0x09, 0x09, 0x09, 0x01},
      {0x3e, 0x41, 0x49, 0x49, 0x7a}, {0x7f, 0x08, 0x08, 0x08, 0x7f}, {0x00, 0x41, 0x7f, 0x41, 0x00},
      {0x20, 0x40, 0x41, 0x3f, 0x01}, {0x7f, 0x08, 0x14, 0x22, 0x41}, {0x7f, 0x40, 0x40, 0x40, 0x40},
      {0x7f, 0x02, 0x0c, 0x02, 0x7f}, {0x7f, 0x04, 0x08, 0x10, 0x7f}, {0x3e, 0x41, 0x41, 0x41, 0x3e},
      {0x7f, 0x09, 0x09, 0x09, 0x06}, {0x3e, 0x41, 0x51, 0x21, 0x5e}, {0x7f, 0x09, 0x19, 0x29, 0x46},
      {0x46, 0x49, 0x49, 0x49, 0x31}, {0x01, 0x01, 0x7f, 0x01, 0x01}, {0x3f, 0x40, 0x40, 0x40, 0x3f},
      {0x1f, 0x20, 0x40, 0x20, 0x1f}, {0x3f, 0x40, 0x38, 0x40, 0x3f}, {0x63, 0x14, 0x08, 0x14, 0x63},
      {0x07, 0x08, 0x70, 0x08, 0x07}, {0x61, 0x51, 0x49, 0x45, 0x43},
  };
  static constexpr uint8_t kColon[5] = {0x00, 0x36, 0x36, 0x00, 0x00};
  static constexpr uint8_t kDash[5] = {0x08, 0x08, 0x08, 0x08, 0x08};
  static constexpr uint8_t kDot[5] = {0x00, 0x60, 0x60, 0x00, 0x00};
  static constexpr uint8_t kSlash[5] = {0x20, 0x10, 0x08, 0x04, 0x02};
  static constexpr uint8_t kPercent[5] = {0x23, 0x13, 0x08, 0x64, 0x62};
  static constexpr uint8_t kPlus[5] = {0x08, 0x08, 0x3e, 0x08, 0x08};
  static constexpr uint8_t kAmp[5] = {0x36, 0x49, 0x55, 0x22, 0x50};
  static constexpr uint8_t kHash[5] = {0x14, 0x7f, 0x14, 0x7f, 0x14};
  static constexpr uint8_t kQuestion[5] = {0x02, 0x01, 0x51, 0x09, 0x06};
  static constexpr uint8_t kBang[5] = {0x00, 0x00, 0x5f, 0x00, 0x00};

  if (c >= '0' && c <= '9') return kDigits[c - '0'];
  if (c >= 'a' && c <= 'z') c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  if (c >= 'A' && c <= 'Z') return kLetters[c - 'A'];
  switch (c) {
    case ':': return kColon;
    case '-':
    case '_': return kDash;
    case '.':
    case ',': return kDot;
    case '/': return kSlash;
    case '%': return kPercent;
    case '+': return kPlus;
    case '&': return kAmp;
    case '#': return kHash;
    case '?': return kQuestion;
    case '!': return kBang;
    default: return kBlank;
  }
}

}  // namespace

void Arduino_GFX::print(char c) {
  if (c == '\n') {
    cursor_x = 0;
    cursor_y += gfx_font ? gfx_font->yAdvance * text_size_y : 8 * text_size_y;
    return;
  }
  if (gfx_font) {
    if (static_cast<uint8_t>(c) < gfx_font->first || static_cast<uint8_t>(c) > gfx_font->last) return;
    const GFXglyph &glyph = gfx_font->glyph[static_cast<uint8_t>(c) - gfx_font->first];
    uint16_t bit = 0;
    uint8_t bits = 0;
    for (uint8_t yy = 0; yy < glyph.height; ++yy) {
      for (uint8_t xx = 0; xx < glyph.width; ++xx) {
        if (!(bit++ & 7)) bits = gfx_font->bitmap[glyph.bitmapOffset + bit / 8];
        if (bits & 0x80) {
          const int16_t px = cursor_x + glyph.xOffset + xx * text_size_x;
          const int16_t py = cursor_y + glyph.yOffset + yy * text_size_y;
          fillRect(px, py, text_size_x, text_size_y, text_color);
        }
        bits <<= 1;
      }
    }
    cursor_x += glyph.xAdvance * text_size_x;
    return;
  }

  const uint8_t *glyph = default_glyph(c);
  if (c != ' ') {
    for (int16_t xx = 0; xx < 5; ++xx) {
      for (int16_t yy = 0; yy < 7; ++yy) {
        if (glyph[xx] & (1u << yy)) {
          fillRect(cursor_x + xx * text_size_x, cursor_y + yy * text_size_y, text_size_x, text_size_y, text_color);
        }
      }
    }
  }
  cursor_x += 6 * text_size_x;
}

void Arduino_GFX::print(const char *s) {
  if (!s) return;
  while (*s) print(*s++);
}

void Arduino_GFX::getTextBounds(const char *s, int16_t x, int16_t y, int16_t *x1, int16_t *y1, uint16_t *w,
                                uint16_t *h) {
  if (!s || !*s) {
    *x1 = x;
    *y1 = y;
    *w = 0;
    *h = 0;
    return;
  }

  if (!gfx_font) {
    const size_t len = std::strlen(s);
    *x1 = x;
    *y1 = y;
    *w = static_cast<uint16_t>(len * 6 * text_size_x);
    *h = static_cast<uint16_t>(8 * text_size_y);
    return;
  }

  int16_t min_x = 32767, min_y = 32767, max_x = -32768, max_y = -32768;
  int16_t cx = x;
  int16_t cy = y;
  for (const char *p = s; *p; ++p) {
    const char c = *p;
    if (c == '\n') {
      cx = x;
      cy += gfx_font->yAdvance * text_size_y;
      continue;
    }
    if (static_cast<uint8_t>(c) < gfx_font->first || static_cast<uint8_t>(c) > gfx_font->last) continue;
    const GFXglyph &glyph = gfx_font->glyph[static_cast<uint8_t>(c) - gfx_font->first];
    if (glyph.width && glyph.height) {
      const int16_t gx1 = cx + glyph.xOffset * text_size_x;
      const int16_t gy1 = cy + glyph.yOffset * text_size_y;
      const int16_t gx2 = gx1 + glyph.width * text_size_x - 1;
      const int16_t gy2 = gy1 + glyph.height * text_size_y - 1;
      min_x = std::min(min_x, gx1);
      min_y = std::min(min_y, gy1);
      max_x = std::max(max_x, gx2);
      max_y = std::max(max_y, gy2);
    }
    cx += glyph.xAdvance * text_size_x;
  }

  if (max_x < min_x || max_y < min_y) {
    *x1 = x;
    *y1 = y;
    *w = 0;
    *h = 0;
    return;
  }
  *x1 = min_x;
  *y1 = min_y;
  *w = static_cast<uint16_t>(max_x - min_x + 1);
  *h = static_cast<uint16_t>(max_y - min_y + 1);
}

void gfx_draw_bitmap_to_framebuffer(uint16_t *bitmap, int16_t w, int16_t h, uint16_t *fb, int16_t x, int16_t y,
                                    int16_t fb_w, int16_t fb_h) {
  for (int16_t yy = 0; yy < h; ++yy) {
    if (y + yy < 0 || y + yy >= fb_h) continue;
    for (int16_t xx = 0; xx < w; ++xx) {
      if (x + xx < 0 || x + xx >= fb_w) continue;
      fb[(y + yy) * fb_w + x + xx] = bitmap[yy * w + xx];
    }
  }
}
void gfx_draw_bitmap_to_framebuffer_rotate_1(uint16_t *b, int16_t w, int16_t h, uint16_t *fb, int16_t x, int16_t y,
                                             int16_t fw, int16_t fh) {
  gfx_draw_bitmap_to_framebuffer(b, w, h, fb, x, y, fw, fh);
}
void gfx_draw_bitmap_to_framebuffer_rotate_2(uint16_t *b, int16_t w, int16_t h, uint16_t *fb, int16_t x, int16_t y,
                                             int16_t fw, int16_t fh) {
  gfx_draw_bitmap_to_framebuffer(b, w, h, fb, x, y, fw, fh);
}
void gfx_draw_bitmap_to_framebuffer_rotate_3(uint16_t *b, int16_t w, int16_t h, uint16_t *fb, int16_t x, int16_t y,
                                             int16_t fw, int16_t fh) {
  gfx_draw_bitmap_to_framebuffer(b, w, h, fb, x, y, fw, fh);
}
