#include "pm_touch.h"

#include <Arduino.h>
#include <Wire.h>

#include "pin_config.h"

#if defined(ASTROLABE_WAVESHARE_S3_185)

static constexpr uint8_t kCst816Addr = 0x15;
static constexpr uint8_t kCst816RegGesture = 0x01;
static constexpr uint8_t kCst816RegDisableAutoSleep = 0xFE;
static constexpr uint8_t kCst816RegVersion = 0x15;
static constexpr uint8_t kCst816RegChipId = 0xA7;
static constexpr uint8_t kTca9554RegOutput = 0x01;
static constexpr uint8_t kTca9554RegConfig = 0x03;

#else
#include "touch/TouchDrvCST92xx.h"

static TouchDrvCST92xx g_touch;
#endif

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
#if defined(ASTROLABE_WAVESHARE_S3_185)
static uint8_t g_hw_gesture = 0;
static int16_t g_hw_x = 0;
static int16_t g_hw_y = 0;
#endif
#if defined(ASTROLABE_WAVESHARE_S3_185) && defined(ASTROLABE_TOUCH_DEBUG)
static uint32_t g_dbg_last_ms = 0;
static int16_t g_dbg_last_x = -1;
static int16_t g_dbg_last_y = -1;
static uint8_t g_dbg_last_n = 0xff;
static bool g_dbg_last_read_ok = true;
#endif

static uint8_t copy_cache(int16_t *xs, int16_t *ys, uint8_t max_pts) {
  const uint8_t copy = g_cache_n < max_pts ? g_cache_n : max_pts;
  for (uint8_t i = 0; i < copy; ++i) {
    xs[i] = g_cache_xs[i];
    ys[i] = g_cache_ys[i];
  }
  return copy;
}

#if defined(ASTROLABE_WAVESHARE_S3_185)
static bool tca9554_write(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(MYNAH_TCA9554_ADDR);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

static bool tca9554_read(uint8_t reg, uint8_t *value) {
  if (!value) {
    return false;
  }
  Wire.beginTransmission(MYNAH_TCA9554_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  if (Wire.requestFrom(static_cast<int>(MYNAH_TCA9554_ADDR), 1) != 1) {
    return false;
  }
  *value = Wire.read();
  return true;
}

static bool tca9554_set_pin(uint8_t pin, bool high) {
  uint8_t out = 0;
  if (!tca9554_read(kTca9554RegOutput, &out)) {
    return false;
  }
  const uint8_t mask = static_cast<uint8_t>(1u << (pin - 1u));
  if (high) {
    out |= mask;
  } else {
    out &= static_cast<uint8_t>(~mask);
  }
  return tca9554_write(kTca9554RegOutput, out);
}

static bool cst816_read(uint8_t reg, uint8_t *data, uint8_t len) {
  if (!data || len == 0) {
    return false;
  }
  Wire.beginTransmission(kCst816Addr);
  Wire.write(reg);
  if (Wire.endTransmission(true) != 0) {
    return false;
  }
  if (Wire.requestFrom(kCst816Addr, len) != len) {
    return false;
  }
  for (uint8_t i = 0; i < len; ++i) {
    data[i] = Wire.read();
  }
  return true;
}

static bool cst816_write(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(kCst816Addr);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission(true) == 0;
}

static bool cst816_reset() {
  if (!tca9554_write(kTca9554RegConfig, 0x00)) {
    return false;
  }
  if (!tca9554_set_pin(MYNAH_EXIO_TOUCH_RST, false)) {
    return false;
  }
  delay(10);
  if (!tca9554_set_pin(MYNAH_EXIO_TOUCH_RST, true)) {
    return false;
  }
  delay(50);
  return true;
}

#if defined(ASTROLABE_TOUCH_DEBUG)
static void touch_debug_print(uint32_t now, bool irq_active, bool read_ok, const uint8_t *buf,
                              uint8_t n) {
  const int16_t x = (read_ok && n > 0) ? static_cast<int16_t>(((buf[2] & 0x0F) << 8) | buf[3]) : -1;
  const int16_t y = (read_ok && n > 0) ? static_cast<int16_t>(((buf[4] & 0x0F) << 8) | buf[5]) : -1;
  const bool changed = n != g_dbg_last_n || x != g_dbg_last_x || y != g_dbg_last_y ||
                       read_ok != g_dbg_last_read_ok;
  if (!changed && now - g_dbg_last_ms < (n > 0 ? 120u : 1000u)) {
    return;
  }
  g_dbg_last_ms = now;
  g_dbg_last_n = n;
  g_dbg_last_x = x;
  g_dbg_last_y = y;
  g_dbg_last_read_ok = read_ok;
  if (read_ok) {
    Serial.printf("touch: irq=%u gesture=0x%02x points=%u x=%d y=%d raw=%02x %02x %02x %02x %02x %02x\n",
                  irq_active ? 1u : 0u, buf[0], static_cast<unsigned>(n), static_cast<int>(x),
                  static_cast<int>(y), buf[0], buf[1], buf[2], buf[3], buf[4], buf[5]);
  } else {
    Serial.printf("touch: irq=%u read=fail\n", irq_active ? 1u : 0u);
  }
}
#endif
#endif

bool pm_touch_begin() {
#ifdef ASTROLABE_QEMU
  g_touch_ok = true;
  return true;
#elif defined(ASTROLABE_WAVESHARE_S3_185)
  pinMode(TP_INT, INPUT_PULLUP);
  g_touch_ok = cst816_reset();
  uint8_t version = 0;
  uint8_t chip[3] = {};
  const bool version_ok = cst816_read(kCst816RegVersion, &version, 1);
  const bool chip_ok = cst816_read(kCst816RegChipId, chip, sizeof(chip));
  if (g_touch_ok) {
    (void)cst816_write(kCst816RegDisableAutoSleep, 10);
  }
#if defined(ASTROLABE_TOUCH_DEBUG)
  Serial.printf("touch: begin reset=%u version_ok=%u version=0x%02x chip_ok=%u chip=%02x %02x %02x int=%u\n",
                g_touch_ok ? 1u : 0u, version_ok ? 1u : 0u, version, chip_ok ? 1u : 0u,
                chip[0], chip[1], chip[2], digitalRead(TP_INT) == LOW ? 1u : 0u);
#endif
  return g_touch_ok;
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

bool pm_touch_consume_hardware_gesture(uint8_t *gesture, int16_t *x, int16_t *y) {
#if defined(ASTROLABE_WAVESHARE_S3_185)
  if (!gesture || !x || !y || g_hw_gesture == 0) {
    return false;
  }
  *gesture = g_hw_gesture;
  *x = g_hw_x;
  *y = g_hw_y;
  g_hw_gesture = 0;
  return true;
#else
  (void)gesture;
  (void)x;
  (void)y;
  return false;
#endif
}

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
#if defined(ASTROLABE_WAVESHARE_S3_185)
  uint8_t buf[6] = {};
  const bool read_ok = cst816_read(kCst816RegGesture, buf, sizeof(buf));
#if defined(ASTROLABE_TOUCH_DEBUG)
  touch_debug_print(now, digitalRead(TP_INT) == LOW, read_ok, buf, read_ok ? (buf[1] & 0x0F) : 0);
#endif
  if (!read_ok) {
    g_cache_n = 0;
    g_cache_ms = now;
    return 0;
  }
  uint8_t n = buf[1] & 0x0F;
  if (n > 1) {
    n = 1;
  }
  g_cache_n = n;
  if (g_cache_n > 0) {
    g_cache_xs[0] = static_cast<int16_t>(((buf[2] & 0x0F) << 8) | buf[3]);
    g_cache_ys[0] = static_cast<int16_t>(((buf[4] & 0x0F) << 8) | buf[5]);
    g_last_points_ms = now;
  }
  if (buf[0] >= 0x01 && buf[0] <= 0x04) {
    g_hw_gesture = buf[0];
    g_hw_x = g_cache_n > 0 ? g_cache_xs[0] : g_hw_x;
    g_hw_y = g_cache_n > 0 ? g_cache_ys[0] : g_hw_y;
  }
  g_cache_ms = now;
#else
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
#endif
  return copy_cache(xs, ys, max_pts);
}
