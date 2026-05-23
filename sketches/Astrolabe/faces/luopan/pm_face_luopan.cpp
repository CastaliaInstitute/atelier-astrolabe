#include "faces/luopan/pm_face_luopan.h"

#include <Arduino_GFX_Library.h>
#include <cmath>
#include <cstdio>

#include "faces/shared/pm_face_draw.h"
#include "pin_config.h"
#include "pm_display.h"
#include "pm_motion.h"

namespace {

float angle_for_deg(float deg) { return (deg - 90.f) * (pm_face_k_pi / 180.f); }

void draw_label(const char *label, int cx, int cy, int r, float deg, uint16_t color, uint8_t size = 1) {
  const float a = angle_for_deg(deg);
  const int x = cx + static_cast<int>(lrintf(cosf(a) * static_cast<float>(r)));
  const int y = cy + static_cast<int>(lrintf(sinf(a) * static_cast<float>(r)));
  pm_gfx->setTextSize(size, size);
  pm_gfx->setTextColor(color);
  int16_t x1, y1;
  uint16_t tw, th;
  pm_gfx->getTextBounds(label, 0, 0, &x1, &y1, &tw, &th);
  pm_gfx->setCursor(x - static_cast<int>(tw) / 2, y - static_cast<int>(th) / 2);
  pm_gfx->print(label);
}

}  // namespace

void pm_face_luopan_tick(uint32_t now_ms) { pm_motion_tick(now_ms); }

void pm_face_luopan_draw(void) {
  pm_face_luopan_tick(millis());

  const int cx = pm_face_lcd_cx;
  const int cy = pm_face_lcd_cy;
  const uint16_t bg = pm_gfx->color565(10, 8, 7);
  const uint16_t red = pm_gfx->color565(178, 44, 38);
  const uint16_t red2 = pm_gfx->color565(96, 28, 24);
  const uint16_t gold = pm_gfx->color565(214, 159, 82);
  const uint16_t jade = pm_gfx->color565(80, 154, 116);
  const uint16_t ink = pm_gfx->color565(238, 219, 176);
  const uint16_t dim = pm_gfx->color565(138, 112, 82);
  const bool have_gyro = pm_motion_has_6dof();
  pm_gfx->fillScreen(bg);

  for (int r = 180; r >= 55; r -= 25) {
    pm_gfx->drawCircle(cx, cy, r, r % 50 == 5 ? red : gold);
  }
  for (int deg = 0; deg < 360; deg += 15) {
    const float a = angle_for_deg(static_cast<float>(deg));
    const bool major = (deg % 45) == 0;
    const int r0 = major ? 54 : 66;
    const int r1 = 180;
    const int x0 = cx + static_cast<int>(lrintf(cosf(a) * static_cast<float>(r0)));
    const int y0 = cy + static_cast<int>(lrintf(sinf(a) * static_cast<float>(r0)));
    const int x1 = cx + static_cast<int>(lrintf(cosf(a) * static_cast<float>(r1)));
    const int y1 = cy + static_cast<int>(lrintf(sinf(a) * static_cast<float>(r1)));
    pm_gfx->drawLine(x0, y0, x1, y1, major ? red : red2);
  }

  static const char *const kBagua[] = {"KAN", "GEN", "ZHEN", "XUN", "LI", "KUN", "DUI", "QIAN"};
  for (int i = 0; i < 8; ++i) {
    draw_label(kBagua[i], cx, cy, 135, static_cast<float>(i) * 45.f, ink);
  }
  static const char *const kMountains[] = {"ZI", "GUI", "CHOU", "GEN", "YIN", "JIA", "MAO", "YI",
                                           "CHEN", "XUN", "SI", "BING", "WU", "DING", "WEI", "KUN",
                                           "SHEN", "GENG", "YOU", "XIN", "XU", "QIAN", "HAI", "REN"};
  for (int i = 0; i < 24; ++i) {
    draw_label(kMountains[i], cx, cy, 164, static_cast<float>(i) * 15.f, i % 3 == 0 ? gold : dim);
  }

  draw_label("N", cx, cy, 92, 0.f, red, 2);
  draw_label("E", cx, cy, 92, 90.f, ink, 2);
  draw_label("S", cx, cy, 92, 180.f, ink, 2);
  draw_label("W", cx, cy, 92, 270.f, ink, 2);

  const float yaw = pm_motion_yaw_deg();
  const float a = angle_for_deg(yaw);
  const int hx = cx + static_cast<int>(lrintf(cosf(a) * 172.f));
  const int hy = cy + static_cast<int>(lrintf(sinf(a) * 172.f));
  pm_gfx->drawLine(cx, cy, hx, hy, red);
  pm_gfx->fillCircle(hx, hy, 5, have_gyro ? red : dim);
  pm_gfx->fillCircle(cx, cy, 30, bg);
  pm_gfx->drawCircle(cx, cy, 31, gold);
  pm_face_draw_centered_line("LUOPAN", cy - 8, gold, 1, 1);
  char buf[32];
  snprintf(buf, sizeof(buf), "%03d rel", static_cast<int>(lrintf(yaw)) % 360);
  pm_face_draw_centered_line(have_gyro ? buf : "relative dial", cy + 12, ink, 1, 1);
  pm_face_draw_centered_line("FENG SHUI DIAL", 34, gold, 1, 1);
  pm_face_draw_centered_line(have_gyro ? "face north + tap to set" : "no magnetometer - face north", 386,
                             have_gyro ? jade : dim, 1, 1);
}
