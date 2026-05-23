#include "pm_display.h"

#include <cstdlib>
#include <cstring>

#include "esp_heap_caps.h"

PmDisplayCanvas *pm_gfx = nullptr;
void pm_display_bind(PmDisplayCanvas *canvas) { pm_gfx = canvas; }

PmDisplayCanvas::PmDisplayCanvas(int16_t w, int16_t h, Arduino_G *output, int16_t output_x, int16_t output_y)
    : Arduino_GFX(w, h), _output(output), _output_x(output_x), _output_y(output_y) {}

PmDisplayCanvas::~PmDisplayCanvas() {
  if (_framebuffer) {
    heap_caps_free(_framebuffer);
  }
}

bool PmDisplayCanvas::begin(int32_t speed) {
  if (speed != GFX_SKIP_OUTPUT_BEGIN && _output && !_output->begin(speed)) {
    return false;
  }
  if (!_framebuffer) {
    const size_t bytes = static_cast<size_t>(WIDTH) * static_cast<size_t>(HEIGHT) * sizeof(uint16_t);
    _framebuffer = static_cast<uint16_t *>(heap_caps_aligned_alloc(16, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!_framebuffer) {
      _framebuffer = static_cast<uint16_t *>(heap_caps_aligned_alloc(16, bytes, MALLOC_CAP_8BIT));
    }
    if (!_framebuffer) {
      return false;
    }
  }
  return true;
}

uint16_t *PmDisplayCanvas::getFramebuffer() { return _framebuffer; }

void PmDisplayCanvas::writePixelPreclipped(int16_t x, int16_t y, uint16_t color) {
  if (!_framebuffer) {
    return;
  }
  uint16_t *fb = _framebuffer;
  switch (_rotation) {
    case 1:
      fb += static_cast<int32_t>(x) * _height;
      fb += _max_y - y;
      *fb = color;
      break;
    case 2:
      fb += static_cast<int32_t>(_max_y - y) * _width;
      fb += _max_x - x;
      *fb = color;
      break;
    case 3:
      fb += static_cast<int32_t>(_max_x - x) * _height;
      fb += y;
      *fb = color;
      break;
    default:
      fb += static_cast<int32_t>(y) * _width;
      fb += x;
      *fb = color;
      break;
  }
}

void PmDisplayCanvas::writeFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) {
  switch (_rotation) {
    case 1:
      writeFastHLineCore(_height - y - h, x, h, color);
      break;
    case 2:
      writeFastVLineCore(_max_x - x, _height - y - h, h, color);
      break;
    case 3:
      writeFastHLineCore(y, _max_x - x, h, color);
      break;
    default:
      writeFastVLineCore(x, y, h, color);
      break;
  }
}

void PmDisplayCanvas::writeFastVLineCore(int16_t x, int16_t y, int16_t h, uint16_t color) {
  if (!_framebuffer || !_ordered_in_range(x, 0, _max_x) || h == 0) {
    return;
  }
  if (h < 0) {
    y += h + 1;
    h = -h;
  }
  if (y > _max_y) {
    return;
  }
  int16_t y2 = y + h - 1;
  if (y2 < 0) {
    return;
  }
  if (y < 0) {
    y = 0;
    h = y2 + 1;
  }
  if (y2 > _max_y) {
    h = _max_y - y + 1;
  }
  uint16_t *fb = _framebuffer + static_cast<int32_t>(y) * WIDTH + x;
  while (h--) {
    *fb = color;
    fb += WIDTH;
  }
}

void PmDisplayCanvas::writeFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) {
  switch (_rotation) {
    case 1:
      writeFastVLineCore(_max_y - y, x, w, color);
      break;
    case 2:
      writeFastHLineCore(_width - x - w, _max_y - y, w, color);
      break;
    case 3:
      writeFastVLineCore(y, _width - x - w, w, color);
      break;
    default:
      writeFastHLineCore(x, y, w, color);
      break;
  }
}

void PmDisplayCanvas::writeFastHLineCore(int16_t x, int16_t y, int16_t w, uint16_t color) {
  if (!_framebuffer || !_ordered_in_range(y, 0, _max_y) || w == 0) {
    return;
  }
  if (w < 0) {
    x += w + 1;
    w = -w;
  }
  if (x > _max_x) {
    return;
  }
  int16_t x2 = x + w - 1;
  if (x2 < 0) {
    return;
  }
  if (x < 0) {
    x = 0;
    w = x2 + 1;
  }
  if (x2 > _max_x) {
    w = _max_x - x + 1;
  }
  uint16_t *fb = _framebuffer + static_cast<int32_t>(y) * WIDTH + x;
  while (w--) {
    *fb++ = color;
  }
}

void PmDisplayCanvas::writeFillRectPreclipped(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
  if (!_framebuffer) {
    return;
  }
  if (_rotation > 0) {
    Arduino_GFX::writeFillRectPreclipped(x, y, w, h, color);
    return;
  }
  uint16_t *row = _framebuffer + static_cast<int32_t>(y) * WIDTH + x;
  for (int16_t j = 0; j < h; ++j) {
    for (int16_t i = 0; i < w; ++i) {
      row[i] = color;
    }
    row += WIDTH;
  }
}

void PmDisplayCanvas::draw16bitRGBBitmap(int16_t x, int16_t y, uint16_t *bitmap, int16_t w, int16_t h) {
  switch (_rotation) {
    case 1:
      gfx_draw_bitmap_to_framebuffer_rotate_1(bitmap, w, h, _framebuffer, x, y, _width, _height);
      break;
    case 2:
      gfx_draw_bitmap_to_framebuffer_rotate_2(bitmap, w, h, _framebuffer, x, y, _width, _height);
      break;
    case 3:
      gfx_draw_bitmap_to_framebuffer_rotate_3(bitmap, w, h, _framebuffer, x, y, _width, _height);
      break;
    default:
      gfx_draw_bitmap_to_framebuffer(bitmap, w, h, _framebuffer, x, y, _width, _height);
      break;
  }
}

void PmDisplayCanvas::draw16bitRGBBitmapWithTranColor(int16_t x, int16_t y, uint16_t *bitmap,
                                                      uint16_t transparent_color, int16_t w, int16_t h) {
  if (_rotation > 0) {
    Arduino_GFX::draw16bitRGBBitmapWithTranColor(x, y, bitmap, transparent_color, w, h);
    return;
  }
  if (!_framebuffer || (x + w - 1) < 0 || (y + h - 1) < 0 || x > _max_x || y > _max_y) {
    return;
  }
  int16_t x_skip = 0;
  if ((y + h - 1) > _max_y) h -= (y + h - 1) - _max_y;
  if (y < 0) {
    bitmap -= y * w;
    h += y;
    y = 0;
  }
  if ((x + w - 1) > _max_x) {
    x_skip = (x + w - 1) - _max_x;
    w -= x_skip;
  }
  if (x < 0) {
    bitmap -= x;
    x_skip -= x;
    w += x;
    x = 0;
  }
  uint16_t *row = _framebuffer + static_cast<int32_t>(y) * _width + x;
  while (h--) {
    for (int16_t i = 0; i < w; ++i) {
      const uint16_t p = *bitmap++;
      if (p != transparent_color) {
        row[i] = p;
      }
    }
    bitmap += x_skip;
    row += _width;
  }
}

void PmDisplayCanvas::draw16bitBeRGBBitmap(int16_t x, int16_t y, uint16_t *bitmap, int16_t w, int16_t h) {
  if (_rotation > 0) {
    Arduino_GFX::draw16bitBeRGBBitmap(x, y, bitmap, w, h);
    return;
  }
  if (!_framebuffer || (x + w - 1) < 0 || (y + h - 1) < 0 || x > _max_x || y > _max_y) {
    return;
  }
  int16_t x_skip = 0;
  if ((y + h - 1) > _max_y) h -= (y + h - 1) - _max_y;
  if (y < 0) {
    bitmap -= y * w;
    h += y;
    y = 0;
  }
  if ((x + w - 1) > _max_x) {
    x_skip = (x + w - 1) - _max_x;
    w -= x_skip;
  }
  if (x < 0) {
    bitmap -= x;
    x_skip -= x;
    w += x;
    x = 0;
  }
  uint16_t *row = _framebuffer + static_cast<int32_t>(y) * _width + x;
  while (h--) {
    for (int16_t i = 0; i < w; ++i) {
      const uint16_t color = *bitmap++;
      row[i] = static_cast<uint16_t>((color >> 8) | (color << 8));
    }
    bitmap += x_skip;
    row += _width;
  }
}

void PmDisplayCanvas::flush(void) {
  if (_output && _framebuffer) {
    _output->draw16bitRGBBitmap(_output_x, _output_y, _framebuffer, WIDTH, HEIGHT);
  }
}
