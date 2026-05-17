#pragma once

#include <mynah_hal/touch.h>

namespace mynah::qemu {

/** Replay swipe/tap sequences from a script file or baked-in CI scenario. */
class TouchScripted : public Touch {
 public:
  bool begin() override;
  uint8_t sample(TouchPoint* out, uint8_t max_pts) override;

  void load_script_path(const char* path);

 private:
  const char* script_path_ = nullptr;
  uint32_t tick_ = 0;
};

TouchScripted& touch_instance();

}  // namespace mynah::qemu
