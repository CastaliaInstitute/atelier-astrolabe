#include "faces/globe/pm_face_globe.h"

#include <Arduino.h>
#include <cmath>

#include "faces/shared/pm_face_draw.h"
#include "pm_display.h"

static uint32_t s_last_ms = 0;
static float s_spin = 0.f;

bool pm_face_globe_anim_tick(uint32_t now_ms) {
  if (s_last_ms == 0) {
    s_last_ms = now_ms;
    return true;
  }
  const uint32_t dt = now_ms - s_last_ms;
  if (dt < 90u) {
    return false;
  }
  s_last_ms = now_ms;
  s_spin += static_cast<float>(dt) * 0.00018f;
  if (s_spin > 6.2831853f) {
    s_spin -= 6.2831853f;
  }
  return true;
}

static float clamp01(float v) {
  if (v < 0.f) {
    return 0.f;
  }
  return v > 1.f ? 1.f : v;
}

void pm_face_globe_draw(const struct tm *local, bool valid_time) {
  const int cx = pm_face_lcd_cx;
  const int cy = pm_face_lcd_cy;
  const int r = 168;
  const uint16_t bg = pm_gfx->color565(2, 6, 16);
  pm_gfx->fillScreen(bg);

  const float sec_day = valid_time && local ? static_cast<float>(local->tm_hour * 3600 + local->tm_min * 60 + local->tm_sec)
                                           : fmodf(static_cast<float>(millis()) * 0.015f, 86400.f);
  const float sun_lon = (sec_day / 86400.f) * 6.2831853f - 3.14159265f;
  const float view_lon = s_spin;

  for (int y = -r; y <= r; y += 3) {
    for (int x = -r; x <= r; x += 3) {
      const float nx = static_cast<float>(x) / static_cast<float>(r);
      const float ny = static_cast<float>(y) / static_cast<float>(r);
      const float d2 = nx * nx + ny * ny;
      if (d2 > 1.f) {
        continue;
      }
      const float nz = sqrtf(1.f - d2);
      const float lon = atan2f(nx, nz) + view_lon;
      const float lat = asinf(-ny);
      const float day = clamp01(0.18f + 0.82f * (cosf(lat) * cosf(lon - sun_lon)));
      const float land = sinf(lon * 2.1f + sinf(lat * 3.2f)) + 0.55f * sinf(lon * 5.4f - lat * 2.3f);
      const bool is_land = land > 0.25f;
      const uint8_t rr = static_cast<uint8_t>((is_land ? 30 : 12) * day + 2);
      const uint8_t gg = static_cast<uint8_t>((is_land ? 120 : 58) * day + 6);
      const uint8_t bb = static_cast<uint8_t>((is_land ? 70 : 150) * day + 20);
      pm_gfx->fillRect(cx + x, cy + y, 3, 3, pm_gfx->color565(rr, gg, bb));
    }
  }

  pm_gfx->drawCircle(cx, cy, r, pm_gfx->color565(150, 190, 230));
  pm_gfx->drawCircle(cx, cy, r + 1, pm_gfx->color565(36, 68, 105));
  for (int i = 0; i < 12; ++i) {
    const float a = static_cast<float>(i) * (6.2831853f / 12.f) + s_spin * 0.2f;
    const int sx = cx + static_cast<int>(cosf(a) * 216.f);
    const int sy = cy + static_cast<int>(sinf(a) * 216.f);
    pm_gfx->drawPixel(sx, sy, pm_gfx->color565(160, 180, 220));
  }
  pm_face_draw_centered_line("Globe", 34, pm_gfx->color565(210, 225, 245), 2, 1);
  pm_face_draw_centered_line(valid_time ? "day / night" : "simulated sun", 398,
                             pm_gfx->color565(130, 150, 175), 1, 1);
}
