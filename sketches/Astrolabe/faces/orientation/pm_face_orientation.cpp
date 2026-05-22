#include "faces/orientation/pm_face_orientation.h"

#include <Arduino_GFX_Library.h>
#include <cmath>
#include <cstdio>

#include "faces/shared/pm_face_draw.h"
#include "pin_config.h"
#include "pm_display.h"
#include "pm_motion.h"

namespace {

constexpr float kDeg = 57.2957795f;

float heading_rad(void) {
  return (pm_motion_yaw_deg() - 90.f) * (pm_face_k_pi / 180.f);
}

void draw_tick_ring(int cx, int cy, int r_outer, int r_inner, uint16_t major, uint16_t minor) {
  for (int deg = 0; deg < 360; deg += 15) {
    const float a = (static_cast<float>(deg) - 90.f) * (pm_face_k_pi / 180.f);
    const bool is_major = (deg % 45) == 0;
    const int r0 = is_major ? r_inner - 10 : r_inner;
    const int x0 = cx + static_cast<int>(lrintf(cosf(a) * static_cast<float>(r0)));
    const int y0 = cy + static_cast<int>(lrintf(sinf(a) * static_cast<float>(r0)));
    const int x1 = cx + static_cast<int>(lrintf(cosf(a) * static_cast<float>(r_outer)));
    const int y1 = cy + static_cast<int>(lrintf(sinf(a) * static_cast<float>(r_outer)));
    pm_gfx->drawLine(x0, y0, x1, y1, is_major ? major : minor);
  }
}

}  // namespace

void pm_face_orientation_tick(uint32_t now_ms) { pm_motion_tick(now_ms); }

void pm_face_orientation_draw(void) {
  pm_face_orientation_tick(millis());

  float ax = 0.f;
  float ay = 0.f;
  float az = 0.f;
  const bool have_accel = pm_motion_accel_norm(&ax, &ay, &az);
  const bool have_heading = pm_motion_has_6dof();
  const float pitch = have_accel ? atan2f(-ax, sqrtf(ay * ay + az * az)) * kDeg : 0.f;
  const float roll = have_accel ? atan2f(ay, az) * kDeg : 0.f;

  const uint16_t bg = pm_gfx->color565(9, 12, 15);
  const uint16_t line = pm_gfx->color565(86, 104, 118);
  const uint16_t dim = pm_gfx->color565(132, 142, 150);
  const uint16_t text = pm_gfx->color565(232, 226, 208);
  const uint16_t gold = pm_gfx->color565(214, 162, 92);
  const uint16_t red = pm_gfx->color565(218, 70, 58);

  pm_gfx->fillScreen(bg);
  const int cx = pm_face_lcd_cx;
  const int cy = pm_face_lcd_cy;
  pm_gfx->drawCircle(cx, cy, 176, line);
  pm_gfx->drawCircle(cx, cy, 142, pm_gfx->color565(42, 52, 58));
  pm_gfx->drawCircle(cx, cy, 82, pm_gfx->color565(38, 44, 48));
  draw_tick_ring(cx, cy, 176, 152, gold, line);

  static const char *const kCard[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
  for (int i = 0; i < 8; ++i) {
    const float a = (static_cast<float>(i) * 45.f - 90.f) * (pm_face_k_pi / 180.f);
    const int x = cx + static_cast<int>(lrintf(cosf(a) * 122.f));
    const int y = cy + static_cast<int>(lrintf(sinf(a) * 122.f));
    pm_gfx->setTextSize(i % 2 == 0 ? 2 : 1, i % 2 == 0 ? 2 : 1);
    pm_gfx->setTextColor(i == 0 ? red : text);
    int16_t x1, y1;
    uint16_t tw, th;
    pm_gfx->getTextBounds(kCard[i], 0, 0, &x1, &y1, &tw, &th);
    pm_gfx->setCursor(x - static_cast<int>(tw) / 2, y - static_cast<int>(th) / 2);
    pm_gfx->print(kCard[i]);
  }

  const float ha = heading_rad();
  const int hx = cx + static_cast<int>(lrintf(cosf(ha) * 112.f));
  const int hy = cy + static_cast<int>(lrintf(sinf(ha) * 112.f));
  pm_gfx->drawLine(cx, cy, hx, hy, red);
  pm_gfx->fillCircle(hx, hy, 7, red);
  pm_gfx->fillCircle(cx, cy, 9, gold);
  pm_gfx->drawCircle(cx, cy, 12, text);

  char buf[40];
  pm_face_draw_centered_line("ORIENTATION", 40, gold, 1, 2);
  snprintf(buf, sizeof(buf), "%03d deg relative", static_cast<int>(lrintf(pm_motion_yaw_deg())) % 360);
  pm_face_draw_centered_line(have_heading ? buf : "IMU unavailable", 200, text, 1, 2);
  snprintf(buf, sizeof(buf), "pitch %+d  roll %+d", static_cast<int>(lrintf(pitch)), static_cast<int>(lrintf(roll)));
  pm_face_draw_centered_line(have_accel ? buf : "waiting for tilt", 232, dim, 1, 1);
  pm_face_draw_centered_line("relative heading - no magnetometer", 382, dim, 1, 1);
}
