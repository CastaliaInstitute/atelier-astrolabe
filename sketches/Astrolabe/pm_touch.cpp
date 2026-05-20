#include "pm_touch.h"

#include <Arduino.h>
#include <Wire.h>

#include "pin_config.h"
#include "touch/TouchDrvCST92xx.h"

static TouchDrvCST92xx g_touch;
static bool g_touch_ok = false;
static int16_t g_cache_xs[5] = {};
static int16_t g_cache_ys[5] = {};
static uint8_t g_cache_n = 0;
static uint32_t g_cache_ms = 0;
static uint32_t g_last_poll_ms = 0;
static uint32_t g_last_points_ms = 0;
static bool g_inject_active = false;
static int16_t g_inject_x = 0;
static int16_t g_inject_y = 0;

static uint8_t copy_cache(int16_t *xs, int16_t *ys, uint8_t max_pts) {
  const uint8_t copy = g_cache_n < max_pts ? g_cache_n : max_pts;
  for (uint8_t i = 0; i < copy; ++i) {
    xs[i] = g_cache_xs[i];
    ys[i] = g_cache_ys[i];
  }
  return copy;
}

bool pm_touch_begin() {
#ifdef ASTROLABE_QEMU
  g_touch_ok = true;
  return true;
#else
  g_touch.setPins(TP_RST, TP_INT);
  g_touch_ok = g_touch.begin(Wire, CST92XX_SLAVE_ADDRESS, IIC_SDA, IIC_SCL);
  pinMode(TP_INT, INPUT);
  return g_touch_ok;
#endif
}

void pm_touch_inject_set(int16_t x, int16_t y) {
  g_inject_active = true;
  g_inject_x = x;
  g_inject_y = y;
  g_cache_xs[0] = x;
  g_cache_ys[0] = y;
  g_cache_n = 1;
  g_cache_ms = millis();
  g_last_points_ms = g_cache_ms;
}

void pm_touch_inject_clear(void) {
  g_inject_active = false;
  g_cache_n = 0;
  g_cache_ms = millis();
}

bool pm_touch_inject_active(void) { return g_inject_active; }

uint8_t pm_touch_sample(int16_t *xs, int16_t *ys, uint8_t max_pts) {
  if (!g_touch_ok || !xs || !ys || max_pts == 0) {
    return 0;
  }
  if (g_inject_active) {
    xs[0] = g_inject_x;
    ys[0] = g_inject_y;
    return 1;
  }
  const uint32_t now = millis();
  if (g_cache_ms != 0 && now - g_cache_ms <= 12u) {
    return copy_cache(xs, ys, max_pts);
  }
#ifndef ASTROLABE_QEMU
  const bool irq_active = digitalRead(TP_INT) == LOW;
  if (!irq_active && now - g_last_points_ms > 70u && now - g_last_poll_ms < 18u) {
    g_cache_n = 0;
    g_cache_ms = now;
    return 0;
  }
#endif
  g_last_poll_ms = now;
  const TouchPoints &tp = g_touch.getTouchPoints();
  const uint8_t n = tp.getPointCount();
  g_cache_n = n < static_cast<uint8_t>(sizeof(g_cache_xs) / sizeof(g_cache_xs[0]))
                  ? n
                  : static_cast<uint8_t>(sizeof(g_cache_xs) / sizeof(g_cache_xs[0]));
  for (uint8_t i = 0; i < g_cache_n; ++i) {
    const TouchPoint &p = tp.getPoint(i);
    g_cache_xs[i] = static_cast<int16_t>(p.x);
    g_cache_ys[i] = static_cast<int16_t>(p.y);
  }
  g_cache_ms = now;
  if (g_cache_n > 0) {
    g_last_points_ms = now;
  }
  return copy_cache(xs, ys, max_pts);
}
