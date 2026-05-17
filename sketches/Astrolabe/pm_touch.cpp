#include "pm_touch.h"

#include <Wire.h>

#include "pin_config.h"
#include "touch/TouchDrvCST92xx.h"

static TouchDrvCST92xx g_touch;
static bool g_touch_ok = false;

bool pm_touch_begin() {
  g_touch.setPins(TP_RST, TP_INT);
  g_touch_ok = g_touch.begin(Wire, CST92XX_SLAVE_ADDRESS, IIC_SDA, IIC_SCL);
  return g_touch_ok;
}

uint8_t pm_touch_sample(int16_t *xs, int16_t *ys, uint8_t max_pts) {
  if (!g_touch_ok || !xs || !ys || max_pts == 0) {
    return 0;
  }
  const TouchPoints &tp = g_touch.getTouchPoints();
  const uint8_t n = tp.getPointCount();
  const uint8_t copy = n < max_pts ? n : max_pts;
  for (uint8_t i = 0; i < copy; ++i) {
    const TouchPoint &p = tp.getPoint(i);
    xs[i] = static_cast<int16_t>(p.x);
    ys[i] = static_cast<int16_t>(p.y);
  }
  return copy;
}
