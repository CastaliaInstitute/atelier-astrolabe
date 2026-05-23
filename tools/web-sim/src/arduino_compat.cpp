#include "Arduino_GFX_Library.h"

#include <emscripten.h>

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

void Arduino_GFX::print(char c) {
  if (c == '\n') {
    cursor_x = 0;
    cursor_y += 8 * text_size_y;
    return;
  }
  const int16_t w = 5 * text_size_x;
  const int16_t h = 7 * text_size_y;
  if (c != ' ') fillRect(cursor_x, cursor_y - h, w, h, text_color);
  cursor_x += 6 * text_size_x;
}

void Arduino_GFX::print(const char *s) {
  if (!s) return;
  while (*s) print(*s++);
}

void Arduino_GFX::getTextBounds(const char *s, int16_t x, int16_t y, int16_t *x1, int16_t *y1, uint16_t *w,
                                uint16_t *h) {
  const size_t len = s ? std::strlen(s) : 0;
  *x1 = x;
  *y1 = y - 7 * text_size_y;
  *w = static_cast<uint16_t>(len * 6 * text_size_x);
  *h = static_cast<uint16_t>(8 * text_size_y);
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

