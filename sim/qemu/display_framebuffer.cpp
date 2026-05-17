#include "display_framebuffer.h"

#include <mynah_hal/board.h>
#include <stdlib.h>
#include <string.h>

#if defined(ESP_PLATFORM)
#include "esp_log.h"
static const char* TAG = "mynah_qemu_disp";
#else
#define ESP_LOGI(tag, fmt, ...) ((void)0)
#endif

namespace mynah::qemu {

static DisplayFramebuffer g_display;

DisplayFramebuffer& display_instance() { return g_display; }

bool DisplayFramebuffer::begin() {
  if (buf_) {
    return true;
  }
  buf_ = static_cast<uint16_t*>(calloc(board::kWidth * board::kHeight, sizeof(uint16_t)));
  return buf_ != nullptr;
}

int DisplayFramebuffer::width() const { return board::kWidth; }

int DisplayFramebuffer::height() const { return board::kHeight; }

uint16_t* DisplayFramebuffer::framebuffer() { return buf_; }

void DisplayFramebuffer::flush() {
  if (dump_path_) {
    ESP_LOGI(TAG, "frame dump requested: %s", dump_path_);
  }
}

void DisplayFramebuffer::set_frame_dump_path(const char* path) { dump_path_ = path; }

}  // namespace mynah::qemu
