#pragma once

#include <mynah_hal/touch.h>

namespace mynah::waveshare {

/** CST92xx touch — wraps sketch pm_touch_* until fully migrated here. */
class TouchCst92xx : public Touch {
 public:
  bool begin() override;
  uint8_t sample(TouchPoint* out, uint8_t max_pts) override;
};

TouchCst92xx& touch_instance();

}  // namespace mynah::waveshare
