#pragma once

#include <Arduino_GFX_Library.h>
#include <cstdint>

class PmDisplayCanvas : public Arduino_GFX {
 public:
  PmDisplayCanvas(int16_t w, int16_t h, Arduino_G *output, int16_t output_x = 0, int16_t output_y = 0);
  ~PmDisplayCanvas();

  bool begin(int32_t speed = GFX_NOT_DEFINED) override;
  void writePixelPreclipped(int16_t x, int16_t y, uint16_t color) override;
  void writeFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) override;
  void writeFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) override;
  void writeFillRectPreclipped(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) override;
  void draw16bitRGBBitmap(int16_t x, int16_t y, uint16_t *bitmap, int16_t w, int16_t h) override;
  void draw16bitRGBBitmapWithTranColor(int16_t x, int16_t y, uint16_t *bitmap, uint16_t transparent_color,
                                       int16_t w, int16_t h) override;
  void draw16bitBeRGBBitmap(int16_t x, int16_t y, uint16_t *bitmap, int16_t w, int16_t h) override;
  void flush(void) override;

  uint16_t *getFramebuffer();

 private:
  void writeFastVLineCore(int16_t x, int16_t y, int16_t h, uint16_t color);
  void writeFastHLineCore(int16_t x, int16_t y, int16_t w, uint16_t color);

  uint16_t *_framebuffer = nullptr;
  Arduino_G *_output = nullptr;
  int16_t _output_x = 0;
  int16_t _output_y = 0;
};

extern PmDisplayCanvas *pm_gfx;
void pm_display_bind(PmDisplayCanvas *canvas);
