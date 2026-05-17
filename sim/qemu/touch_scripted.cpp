#include "touch_scripted.h"

namespace mynah::qemu {

static TouchScripted g_touch;

TouchScripted& touch_instance() { return g_touch; }

bool TouchScripted::begin() { return true; }

void TouchScripted::load_script_path(const char* path) { script_path_ = path; }

uint8_t TouchScripted::sample(TouchPoint* out, uint8_t max_pts) {
  (void)script_path_;
  (void)tick_;
  if (!out || max_pts == 0) {
    return 0;
  }
  return 0;
}

}  // namespace mynah::qemu
