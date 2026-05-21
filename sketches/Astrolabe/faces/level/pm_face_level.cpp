#include "faces/level/pm_face_level.h"

#include <Arduino_GFX_Library.h>
#include <cmath>
#include <cstdio>

#include "faces/shared/pm_face_draw.h"
#include "pin_config.h"
#include "pm_display.h"
#include "pm_motion.h"

namespace {

constexpr float kDeadband = 0.075f;
constexpr float kMaxTilt = 0.62f;

float s_ball_x = 0.f;
float s_ball_y = 0.f;
bool s_have_sample = false;
uint32_t s_last_tick_ms = 0;
char s_guidance[48] = "level";

float clampf(float v, float lo, float hi) {
  if (v < lo) {
    return lo;
  }
  if (v > hi) {
    return hi;
  }
  return v;
}

void update_guidance(void) {
  const float ax = fabsf(s_ball_x);
  const float ay = fabsf(s_ball_y);
  const float mag = sqrtf(s_ball_x * s_ball_x + s_ball_y * s_ball_y);
  if (!s_have_sample) {
    snprintf(s_guidance, sizeof(s_guidance), "IMU unavailable");
    return;
  }
  if (mag < kDeadband) {
    snprintf(s_guidance, sizeof(s_guidance), "level");
    return;
  }
  const char *amount = mag < 0.30f ? "a little" : "more";
  if (ax > ay * 1.35f) {
    snprintf(s_guidance, sizeof(s_guidance), "%s to the %s", amount, s_ball_x < 0.f ? "left" : "right");
  } else if (ay > ax * 1.35f) {
    snprintf(s_guidance, sizeof(s_guidance), "%s %s", amount, s_ball_y < 0.f ? "forward" : "back");
  } else {
    snprintf(s_guidance, sizeof(s_guidance), "%s %s and %s", amount, s_ball_y < 0.f ? "forward" : "back",
             s_ball_x < 0.f ? "left" : "right");
  }
}

void draw_sphere(int cx, int cy, int r) {
  const uint16_t shadow = pm_gfx->color565(4, 5, 8);
  const uint16_t edge = pm_gfx->color565(230, 238, 244);
  const uint16_t body = pm_gfx->color565(96, 210, 232);
  const uint16_t mid = pm_gfx->color565(72, 150, 198);
  const uint16_t dark = pm_gfx->color565(26, 54, 86);
  pm_gfx->fillCircle(cx + 5, cy + 7, r, shadow);
  for (int rr = r; rr > 0; --rr) {
    const float t = static_cast<float>(rr) / static_cast<float>(r);
    const uint16_t col = t > 0.68f ? dark : (t > 0.38f ? mid : body);
    pm_gfx->fillCircle(cx, cy, rr, col);
  }
  pm_gfx->fillCircle(cx - r / 3, cy - r / 3, r / 4, pm_gfx->color565(218, 252, 255));
  pm_gfx->drawCircle(cx, cy, r, edge);
}

}  // namespace

bool pm_face_level_anim_tick(uint32_t now_ms) {
  pm_motion_tick(now_ms);
  float ax = 0.f;
  float ay = 0.f;
  float az = 0.f;
  const bool ok = pm_motion_accel_norm(&ax, &ay, &az);
  float target_x = 0.f;
  float target_y = 0.f;
  if (ok) {
    target_x = clampf(ay / kMaxTilt, -1.f, 1.f);
    target_y = clampf(ax / kMaxTilt, -1.f, 1.f);
  }
  const float alpha = s_have_sample ? 0.22f : 1.f;
  s_ball_x += (target_x - s_ball_x) * alpha;
  s_ball_y += (target_y - s_ball_y) * alpha;
  s_have_sample = ok;
  s_last_tick_ms = now_ms;
  update_guidance();
  return true;
}

void pm_face_level_draw(void) {
  if (s_last_tick_ms == 0) {
    (void)pm_face_level_anim_tick(millis());
  }

  const uint16_t c_bg = pm_gfx->color565(8, 13, 18);
  const uint16_t c_grid = pm_gfx->color565(34, 54, 64);
  const uint16_t c_grid2 = pm_gfx->color565(20, 34, 42);
  const uint16_t c_text = pm_gfx->color565(228, 236, 226);
  const uint16_t c_dim = pm_gfx->color565(138, 158, 166);
  const uint16_t c_accent = pm_gfx->color565(164, 220, 136);

  pm_gfx->fillScreen(c_bg);
  const int cx = pm_face_lcd_cx;
  const int cy = pm_face_lcd_cy;
  constexpr int r_track = 158;
  constexpr int r_ball = 24;
  pm_gfx->drawCircle(cx, cy, r_track, c_grid);
  pm_gfx->drawCircle(cx, cy, r_track - 32, c_grid2);
  pm_gfx->drawCircle(cx, cy, 30, pm_gfx->color565(68, 96, 76));
  pm_gfx->drawLine(cx - r_track, cy, cx + r_track, cy, c_grid2);
  pm_gfx->drawLine(cx, cy - r_track, cx, cy + r_track, c_grid2);
  pm_gfx->drawLine(cx - 13, cy, cx + 13, cy, c_accent);
  pm_gfx->drawLine(cx, cy - 13, cx, cy + 13, c_accent);

  pm_face_draw_centered_line("LEVEL", 46, c_accent, 2, 2);
  pm_face_draw_centered_line("FORWARD", 79, c_dim, 1, 1);

  const int travel = r_track - r_ball - 7;
  int bx = cx + static_cast<int>(lrintf(s_ball_x * static_cast<float>(travel)));
  int by = cy + static_cast<int>(lrintf(s_ball_y * static_cast<float>(travel)));
  const int dx = bx - cx;
  const int dy = by - cy;
  const float dist = sqrtf(static_cast<float>(dx * dx + dy * dy));
  if (dist > static_cast<float>(travel)) {
    const float scale = static_cast<float>(travel) / dist;
    bx = cx + static_cast<int>(lrintf(static_cast<float>(dx) * scale));
    by = cy + static_cast<int>(lrintf(static_cast<float>(dy) * scale));
  }
  draw_sphere(bx, by, r_ball);

  pm_face_draw_centered_line(pm_face_level_guidance(), 354, c_text, 1, 1);
  pm_face_draw_centered_line(s_have_sample ? "BOOT speaks the nudge" : "waiting for IMU", 382, c_dim, 1, 1);
}

const char *pm_face_level_guidance(void) {
  update_guidance();
  return s_guidance;
}

bool pm_face_level_offset(float *x, float *y) {
  if (x) {
    *x = s_ball_x;
  }
  if (y) {
    *y = s_ball_y;
  }
  return s_have_sample;
}
