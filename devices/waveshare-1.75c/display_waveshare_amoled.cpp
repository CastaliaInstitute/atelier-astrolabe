#include "display_waveshare_amoled.h"

#include <Arduino_GFX_Library.h>

#include "board_config.h"
#include <mynah_hal/board.h>

namespace mynah::waveshare {

static DisplayAmoled g_display;

DisplayAmoled& display_instance() { return g_display; }

void DisplayAmoled::bind(Arduino_Canvas* canvas) { canvas_ = canvas; }

bool DisplayAmoled::begin() { return canvas_ != nullptr; }

int DisplayAmoled::width() const {
  return canvas_ ? static_cast<int>(canvas_->width()) : board::kWidth;
}

int DisplayAmoled::height() const {
  return canvas_ ? static_cast<int>(canvas_->height()) : board::kHeight;
}

uint16_t* DisplayAmoled::framebuffer() {
  return canvas_ ? reinterpret_cast<uint16_t*>(canvas_->getFramebuffer()) : nullptr;
}

void DisplayAmoled::flush() {
  if (canvas_) {
    canvas_->flush();
  }
}

}  // namespace mynah::waveshare
