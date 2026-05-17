#include "faces/classic_analog/pm_face_classic_analog.h"
#include "faces/shared/pm_face_draw.h"
#include <cmath>
#include "pin_config.h"
#include "pm_config.h"
#include "pm_display.h"

constexpr int pm_face_analog_cx = LCD_WIDTH / 2;
constexpr int pm_face_analog_cy = LCD_HEIGHT / 2;
constexpr int pm_face_analog_r = 138;
constexpr int pm_face_analog_sec_len = pm_face_analog_r - 10;

void pm_face_classic_analog_draw(uint16_t bg565, const struct tm *tm, bool valid) {
#if MYNAH_HUE_HOME_ONLY
  (void)bg565;
  (void)tm;
  (void)valid;
  return;
#else
  const int cx = pm_face_analog_cx;
  const int cy = pm_face_analog_cy;
  const int r = pm_face_analog_r;

  const uint16_t tick_major = pm_gfx->color565(230, 232, 250);
  const uint16_t tick_minor = pm_gfx->color565(120, 125, 150);
  for (int h = 0; h < 12; ++h) {
    const float ang = h * (pm_face_k_two_pi / 12.f) - pm_face_k_pi * 0.5f;
    const bool major = (h % 3) == 0;
    const int r0 = r - 2;
    const int r1 = r - (major ? 14 : 8);
    const int x0 = cx + static_cast<int>(lrintf(cosf(ang) * static_cast<float>(r0)));
    const int y0 = cy + static_cast<int>(lrintf(sinf(ang) * static_cast<float>(r0)));
    const int x1 = cx + static_cast<int>(lrintf(cosf(ang) * static_cast<float>(r1)));
    const int y1 = cy + static_cast<int>(lrintf(sinf(ang) * static_cast<float>(r1)));
    pm_gfx->drawLine(x0, y0, x1, y1, major ? tick_major : tick_minor);
  }

  float h_ang;
  float m_ang;
  float s_ang;
  if (valid) {
    const float hf =
        static_cast<float>(tm->tm_hour % 12) + static_cast<float>(tm->tm_min) / 60.f +
        static_cast<float>(tm->tm_sec) / 3600.f;
    h_ang = hf * (pm_face_k_two_pi / 12.f) - pm_face_k_pi * 0.5f;
    m_ang =
        (static_cast<float>(tm->tm_min) + static_cast<float>(tm->tm_sec) / 60.f) * (pm_face_k_two_pi / 60.f) -
        pm_face_k_pi * 0.5f;
    s_ang = static_cast<float>(tm->tm_sec) * (pm_face_k_two_pi / 60.f) - pm_face_k_pi * 0.5f;
  } else {
    h_ang = m_ang = s_ang = -pm_face_k_pi * 0.5f;
  }

  const uint16_t c_hour = pm_gfx->color565(210, 218, 255);
  const uint16_t c_min = RGB565_WHITE;
  const uint16_t c_sec = pm_gfx->color565(255, 95, 95);

  pm_face_draw_hand_radial(cx, cy, h_ang, r - 52, c_hour, 3);
  pm_face_draw_hand_radial(cx, cy, m_ang, r - 22, c_min, 2);
  pm_face_draw_hand_radial(cx, cy, s_ang, pm_face_analog_sec_len, c_sec, 1);

  pm_gfx->fillCircle(cx, cy, 7, c_hour);
  pm_gfx->fillCircle(cx, cy, 3, bg565);
#endif
}


