#pragma once

#include <mynah_hal/display.h>

namespace mynah::qemu {

/** Null or RAM framebuffer for ESP-IDF QEMU — no AMOLED timing. */
class DisplayFramebuffer : public Display {
 public:
  bool begin() override;
  int width() const override;
  int height() const override;
  uint16_t* framebuffer() override;
  void flush() override;

  /** Dump current frame to path (PNG via host tool) when MYNAH_QEMU_FRAME_DUMP=1. */
  void set_frame_dump_path(const char* path);

 private:
  uint16_t* buf_ = nullptr;
  const char* dump_path_ = nullptr;
};

DisplayFramebuffer& display_instance();

}  // namespace mynah::qemu
