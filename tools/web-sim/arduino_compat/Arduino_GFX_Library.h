#pragma once

#include "Arduino.h"

#include <cmath>
#include <cstdint>

#define RGB565_BLACK 0x0000
#define RGB565_WHITE 0xffff
#define RGB565_RED 0xf800
#define RGB565_GREEN 0x07e0
#define RGB565_BLUE 0x001f
#define GFX_NOT_DEFINED -1
#define GFX_SKIP_OUTPUT_BEGIN -2

struct GFXglyph {
  uint16_t bitmapOffset;
  uint8_t width;
  uint8_t height;
  uint8_t xAdvance;
  int8_t xOffset;
  int8_t yOffset;
};

struct GFXfont {
  uint8_t *bitmap;
  GFXglyph *glyph;
  uint16_t first;
  uint16_t last;
  uint8_t yAdvance;
};

class Arduino_G {
 public:
  virtual ~Arduino_G() = default;
  virtual bool begin(int32_t = GFX_NOT_DEFINED) { return true; }
  virtual void draw16bitRGBBitmap(int16_t, int16_t, uint16_t *, int16_t, int16_t) {}
};

class Arduino_GFX : public Arduino_G {
 public:
  Arduino_GFX(int16_t w, int16_t h) : WIDTH(w), HEIGHT(h), _width(w), _height(h), _max_x(w - 1), _max_y(h - 1) {}
  bool begin(int32_t = GFX_NOT_DEFINED) override { return true; }
  virtual void writePixelPreclipped(int16_t x, int16_t y, uint16_t color) { (void)x; (void)y; (void)color; }
  virtual void writeFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) {
    for (int16_t i = 0; i < h; ++i) writePixel(x, y + i, color);
  }
  virtual void writeFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) {
    for (int16_t i = 0; i < w; ++i) writePixel(x + i, y, color);
  }
  virtual void writeFillRectPreclipped(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    for (int16_t j = 0; j < h; ++j) writeFastHLine(x, y + j, w, color);
  }
  virtual void draw16bitRGBBitmap(int16_t x, int16_t y, uint16_t *bitmap, int16_t w, int16_t h) override {
    for (int16_t yy = 0; yy < h; ++yy) {
      for (int16_t xx = 0; xx < w; ++xx) writePixel(x + xx, y + yy, bitmap[yy * w + xx]);
    }
  }
  virtual void draw16bitRGBBitmapWithTranColor(int16_t x, int16_t y, uint16_t *bitmap, uint16_t transparent_color,
                                               int16_t w, int16_t h) {
    for (int16_t yy = 0; yy < h; ++yy) {
      for (int16_t xx = 0; xx < w; ++xx) {
        const uint16_t c = bitmap[yy * w + xx];
        if (c != transparent_color) writePixel(x + xx, y + yy, c);
      }
    }
  }
  virtual void draw16bitBeRGBBitmap(int16_t x, int16_t y, uint16_t *bitmap, int16_t w, int16_t h) {
    for (int16_t yy = 0; yy < h; ++yy) {
      for (int16_t xx = 0; xx < w; ++xx) {
        const uint16_t c = bitmap[yy * w + xx];
        writePixel(x + xx, y + yy, static_cast<uint16_t>((c >> 8) | (c << 8)));
      }
    }
  }
  virtual void flush(void) {}

  void drawPixel(int16_t x, int16_t y, uint16_t color) { writePixel(x, y, color); }
  void writePixel(int16_t x, int16_t y, uint16_t color) {
    if (_ordered_in_range(x, 0, _max_x) && _ordered_in_range(y, 0, _max_y)) writePixelPreclipped(x, y, color);
  }
  void fillScreen(uint16_t color) { fillRect(0, 0, _width, _height, color); }
  void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
  void drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
  void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) { writeFastHLine(x, y, w, color); }
  void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) { writeFastVLine(x, y, h, color); }
  void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color);
  void drawCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color);
  void fillCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color);
  void drawEllipse(int16_t x0, int16_t y0, int16_t rx, int16_t ry, uint16_t color);
  void fillEllipse(int16_t x0, int16_t y0, int16_t rx, int16_t ry, uint16_t color);
  void drawTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t color);
  void fillTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t color);
  void drawRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color);
  void fillRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color);

  void setCursor(int16_t x, int16_t y) { cursor_x = x; cursor_y = y; }
  void setTextColor(uint16_t c) { text_color = c; }
  void setTextColor(uint16_t c, uint16_t) { text_color = c; }
  void setTextSize(uint8_t s) { text_size_x = text_size_y = s ? s : 1; }
  void setTextSize(uint8_t sx, uint8_t sy) { text_size_x = sx ? sx : 1; text_size_y = sy ? sy : 1; }
  void setFont(const GFXfont *font = nullptr) { gfx_font = font; (void)gfx_font; }
  void print(const char *s);
  void print(char c);
  void getTextBounds(const char *s, int16_t x, int16_t y, int16_t *x1, int16_t *y1, uint16_t *w, uint16_t *h);
  uint16_t color565(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3));
  }
  int16_t width(void) const { return _width; }
  int16_t height(void) const { return _height; }

 protected:
  bool _ordered_in_range(int16_t v, int16_t lo, int16_t hi) const { return v >= lo && v <= hi; }
  int16_t WIDTH;
  int16_t HEIGHT;
  int16_t _width;
  int16_t _height;
  int16_t _max_x;
  int16_t _max_y;
  uint8_t _rotation = 0;
  int16_t cursor_x = 0;
  int16_t cursor_y = 0;
  uint8_t text_size_x = 1;
  uint8_t text_size_y = 1;
  uint16_t text_color = RGB565_WHITE;
  const GFXfont *gfx_font = nullptr;
};

void gfx_draw_bitmap_to_framebuffer(uint16_t *bitmap, int16_t w, int16_t h, uint16_t *fb, int16_t x, int16_t y,
                                    int16_t fb_w, int16_t fb_h);
void gfx_draw_bitmap_to_framebuffer_rotate_1(uint16_t *bitmap, int16_t w, int16_t h, uint16_t *fb, int16_t x,
                                             int16_t y, int16_t fb_w, int16_t fb_h);
void gfx_draw_bitmap_to_framebuffer_rotate_2(uint16_t *bitmap, int16_t w, int16_t h, uint16_t *fb, int16_t x,
                                             int16_t y, int16_t fb_w, int16_t fb_h);
void gfx_draw_bitmap_to_framebuffer_rotate_3(uint16_t *bitmap, int16_t w, int16_t h, uint16_t *fb, int16_t x,
                                             int16_t y, int16_t fb_w, int16_t fb_h);
