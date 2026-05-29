#pragma once

#include <cstdint>

namespace mynah {

struct TouchPoint {
  int16_t x = 0;
  int16_t y = 0;
  /** True while finger/stylus is down. */
  bool down = false;
};

class Touch {
 public:
  virtual ~Touch() = default;
  virtual bool begin() = 0;
  /** Returns number of points written (0..max_pts). */
  virtual uint8_t sample(TouchPoint* out, uint8_t max_pts) = 0;
};

}  // namespace mynah
