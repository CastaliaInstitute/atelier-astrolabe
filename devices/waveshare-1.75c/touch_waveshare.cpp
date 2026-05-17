#include "touch_waveshare.h"

// Sketch must link `pm_touch.cpp` when this translation unit is compiled.
extern bool pm_touch_begin();
extern uint8_t pm_touch_sample(int16_t* xs, int16_t* ys, uint8_t max_pts);

namespace mynah::waveshare {

static TouchCst92xx g_touch;

TouchCst92xx& touch_instance() { return g_touch; }

bool TouchCst92xx::begin() { return pm_touch_begin(); }

uint8_t TouchCst92xx::sample(TouchPoint* out, uint8_t max_pts) {
  if (!out || max_pts == 0) {
    return 0;
  }
  int16_t xs[8];
  int16_t ys[8];
  const uint8_t n = pm_touch_sample(xs, ys, max_pts < 8 ? max_pts : 8);
  for (uint8_t i = 0; i < n; ++i) {
    out[i].x = xs[i];
    out[i].y = ys[i];
    out[i].down = true;
  }
  return n;
}

}  // namespace mynah::waveshare
