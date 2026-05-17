#pragma once

#include <mynah_hal/display.h>

class Arduino_Canvas;

namespace mynah::waveshare {

/**
 * Bridges Arduino_GFX canvas (CO5300 + QSPI) to mynah::Display.
 * Call bind() from setup after constructing the global canvas in the sketch.
 */
class DisplayAmoled : public Display {
 public:
  void bind(Arduino_Canvas* canvas);

  bool begin() override;
  int width() const override;
  int height() const override;
  uint16_t* framebuffer() override;
  void flush() override;

 private:
  Arduino_Canvas* canvas_ = nullptr;
};

DisplayAmoled& display_instance();

}  // namespace mynah::waveshare
