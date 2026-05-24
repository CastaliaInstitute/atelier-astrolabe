#include "faces/sky/pm_face_sky.h"

#include <Arduino.h>
#include <cmath>
#include <cstdio>

#include "faces/shared/pm_face_draw.h"
#include "pm_display.h"
#include "pm_touch.h"

struct SkyStar {
  const char *id;
  float ra;
  float dec;
  float mag;
};

struct SkySeg {
  uint8_t a;
  uint8_t b;
};

static const SkyStar kStars[] = {
    {"Sirius", 101.287f, -16.716f, -1.46f}, {"Canopus", 95.988f, -52.696f, -0.74f},
    {"Arcturus", 213.915f, 19.182f, -0.05f}, {"Vega", 279.235f, 38.784f, 0.03f},
    {"Capella", 79.172f, 45.998f, 0.08f}, {"Rigel", 78.634f, -8.202f, 0.13f},
    {"Procyon", 114.825f, 5.225f, 0.34f}, {"Betelgeuse", 88.793f, 7.407f, 0.42f},
    {"Hadar", 210.956f, -60.373f, 0.61f}, {"Altair", 297.695f, 8.868f, 0.76f},
    {"Acrux", 186.65f, -63.099f, 0.76f}, {"Aldebaran", 68.98f, 16.509f, 0.85f},
    {"Antares", 247.352f, -26.432f, 0.96f}, {"Spica", 201.298f, -11.161f, 0.97f},
    {"Pollux", 116.329f, 28.026f, 1.14f}, {"Regulus", 152.093f, 11.967f, 1.35f},
    {"Adhara", 104.656f, -28.972f, 1.5f}, {"Castor", 113.649f, 31.888f, 1.57f},
    {"Bellatrix", 81.283f, 6.35f, 1.64f}, {"Elnath", 81.573f, 28.607f, 1.65f},
    {"Miaplacidus", 138.3f, -69.717f, 1.67f}, {"Alnilam", 84.053f, -1.202f, 1.69f},
    {"Alnitak", 85.19f, -1.943f, 1.74f}, {"Dubhe", 165.932f, 61.751f, 1.81f},
    {"Wezen", 111.024f, -26.393f, 1.83f}, {"Alkaid", 206.885f, 49.313f, 1.85f},
    {"Sargas", 264.395f, -42.998f, 1.86f}, {"Avior", 125.628f, -59.509f, 1.86f},
    {"Atria", 252.166f, -69.028f, 1.91f}, {"Mirzam", 95.675f, -17.956f, 1.98f},
    {"Polaris", 37.954f, 89.264f, 1.98f}, {"Gacrux", 187.791f, -57.113f, 2.06f},
    {"Saiph", 86.939f, -9.67f, 2.07f}, {"Kochab", 222.676f, 74.155f, 2.07f},
    {"Algieba", 154.993f, 19.842f, 2.08f}, {"Denebola", 177.265f, 14.572f, 2.14f},
    {"Mizar", 200.981f, 54.925f, 2.23f}, {"Merak", 165.46f, 56.382f, 2.34f},
    {"Phecda", 178.457f, 53.695f, 2.41f},
};

static const SkySeg kSegments[] = {
    {7, 18}, {18, 22}, {7, 22}, {22, 21}, {21, 5}, {5, 32}, {23, 37}, {37, 38},
    {38, 36}, {36, 25}, {23, 36}, {30, 33}, {15, 34}, {34, 35}, {17, 14}, {11, 19},
    {0, 24}, {24, 16}, {16, 29}, {12, 26}, {10, 31}, {31, 28}, {28, 20}, {20, 27},
    {27, 28}, {8, 20}, {8, 10},
};

static int32_t s_offset_min = 0;
static bool s_touching = false;
static float s_last_ang = 0.f;
static uint32_t s_last_anim_ms = 0;

static float wrap_pi(float v) {
  while (v > 3.14159265f) {
    v -= 6.2831853f;
  }
  while (v < -3.14159265f) {
    v += 6.2831853f;
  }
  return v;
}

static float touch_angle(int16_t x, int16_t y) {
  return atan2f(static_cast<float>(y - pm_face_lcd_cy), static_cast<float>(x - pm_face_lcd_cx));
}

bool pm_face_sky_touch_tick(uint32_t now_ms) {
  (void)now_ms;
  int16_t xs[1];
  int16_t ys[1];
  if (pm_touch_sample(xs, ys, 1) == 0) {
    s_touching = false;
    return false;
  }
  const float dx = static_cast<float>(xs[0] - pm_face_lcd_cx);
  const float dy = static_cast<float>(ys[0] - pm_face_lcd_cy);
  const float r = sqrtf(dx * dx + dy * dy);
  if (r < static_cast<float>(pm_face_scale_i(150))) {
    return false;
  }
  const float a = touch_angle(xs[0], ys[0]);
  if (!s_touching) {
    s_touching = true;
    s_last_ang = a;
    return false;
  }
  const float da = wrap_pi(a - s_last_ang);
  s_last_ang = a;
  s_offset_min += static_cast<int32_t>(da * (1440.f / 6.2831853f));
  if (s_offset_min > 4320) {
    s_offset_min -= 4320;
  } else if (s_offset_min < -4320) {
    s_offset_min += 4320;
  }
  return true;
}

bool pm_face_sky_anim_tick(uint32_t now_ms) {
  if (now_ms - s_last_anim_ms < 250u) {
    return false;
  }
  s_last_anim_ms = now_ms;
  return s_touching;
}

void pm_face_sky_on_leave(void) {
  s_touching = false;
}

static bool project_star(const SkyStar &s, float sidereal_deg, int *x, int *y) {
  const float hour = (sidereal_deg - s.ra) * 0.0174532925f;
  const float dec = s.dec * 0.0174532925f;
  const float alt_proxy = sinf(dec) * 0.35f + cosf(dec) * cosf(hour) * 0.65f;
  if (alt_proxy < -0.18f) {
    return false;
  }
  const float rr = (1.f - alt_proxy) * static_cast<float>(pm_face_scale_i(150));
  const float az = atan2f(sinf(hour), cosf(hour) * sinf(dec) + 0.24f);
  *x = pm_face_lcd_cx + static_cast<int>(sinf(az) * rr);
  *y = pm_face_lcd_cy - static_cast<int>(cosf(az) * rr);
  const int dx = *x - pm_face_lcd_cx;
  const int dy = *y - pm_face_lcd_cy;
  const int horizon = pm_face_scale_i(178);
  return dx * dx + dy * dy < horizon * horizon;
}

void pm_face_sky_draw(const struct tm *local, bool valid_time) {
  pm_gfx->fillScreen(pm_gfx->color565(1, 3, 13));
  const int min_day = valid_time && local ? local->tm_hour * 60 + local->tm_min : (millis() / 1000) % 1440;
  const int shown_min = (min_day + static_cast<int>(s_offset_min) + 14400) % 1440;
  const float sidereal = fmodf(static_cast<float>(shown_min) * 0.25f + 110.f, 360.f);

  const uint16_t grid = pm_gfx->color565(22, 34, 58);
  const uint16_t line_col = pm_gfx->color565(70, 92, 138);
  const uint16_t star_col = pm_gfx->color565(220, 228, 255);
  pm_gfx->drawCircle(pm_face_lcd_cx, pm_face_lcd_cy, pm_face_scale_i(178), grid);
  pm_gfx->drawCircle(pm_face_lcd_cx, pm_face_lcd_cy, pm_face_scale_i(118), grid);
  pm_gfx->drawLine(pm_face_lcd_cx, pm_face_scale_y(56), pm_face_lcd_cx, pm_face_scale_y(408), grid);
  pm_gfx->drawLine(pm_face_scale_x(56), pm_face_lcd_cy, pm_face_scale_x(408), pm_face_lcd_cy, grid);

  int sx[sizeof(kStars) / sizeof(kStars[0])];
  int sy[sizeof(kStars) / sizeof(kStars[0])];
  bool vis[sizeof(kStars) / sizeof(kStars[0])];
  for (size_t i = 0; i < sizeof(kStars) / sizeof(kStars[0]); ++i) {
    vis[i] = project_star(kStars[i], sidereal, &sx[i], &sy[i]);
  }
  for (size_t i = 0; i < sizeof(kSegments) / sizeof(kSegments[0]); ++i) {
    if (vis[kSegments[i].a] && vis[kSegments[i].b]) {
      pm_gfx->drawLine(sx[kSegments[i].a], sy[kSegments[i].a], sx[kSegments[i].b], sy[kSegments[i].b], line_col);
    }
  }
  for (size_t i = 0; i < sizeof(kStars) / sizeof(kStars[0]); ++i) {
    if (!vis[i]) {
      continue;
    }
    const int r = pm_face_scale_i(kStars[i].mag < 0.5f ? 3 : (kStars[i].mag < 1.8f ? 2 : 1));
    pm_gfx->fillCircle(sx[i], sy[i], r, star_col);
  }

  char line[40];
  snprintf(line, sizeof(line), "%02d:%02d", shown_min / 60, shown_min % 60);
  pm_face_draw_centered_line("Sky", 34, pm_gfx->color565(210, 220, 245), 2, 1);
  pm_face_draw_centered_line(line, 398, pm_gfx->color565(160, 180, 220), 1, 1);
}
