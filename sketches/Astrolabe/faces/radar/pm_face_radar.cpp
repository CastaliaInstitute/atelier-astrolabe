#include "faces/radar/pm_face_radar.h"

#include <cmath>
#include <cstdio>

#include "faces/shared/pm_face_draw.h"
#include "pin_config.h"
#include "pm_display.h"
#include "pm_motion.h"
#include "pm_presence.h"

static uint32_t s_last_motion_ms = 0;
static float s_sweep_deg = 0.f;

static int rssi_to_radius_px(int8_t rssi_ema, int rmax) {
  const int ring = pm_presence_rssi_ring(rssi_ema);
  return (rmax * (ring + 1)) / 5;
}

void pm_face_radar_on_enter(void) {
  s_last_motion_ms = 0;
  s_sweep_deg = 0.f;
}

void pm_face_radar_on_leave(void) {}

void pm_face_radar_tick(uint32_t now_ms) {
  if (s_last_motion_ms == 0) {
    s_last_motion_ms = now_ms;
    return;
  }
  static float s_prev_yaw = 0.f;
  pm_motion_tick(now_ms);
  if (pm_motion_has_gyro()) {
    const float yaw = pm_motion_yaw_deg();
    float delta = yaw - s_prev_yaw;
    while (delta > 180.f) {
      delta -= 360.f;
    }
    while (delta < -180.f) {
      delta += 360.f;
    }
    pm_presence_apply_yaw_delta(delta);
    s_prev_yaw = yaw;
  }
  s_sweep_deg += 2.5f;
  if (s_sweep_deg >= 360.f) {
    s_sweep_deg -= 360.f;
  }
  s_last_motion_ms = now_ms;
}

void pm_face_radar_draw(const struct tm *tm, bool valid) {
  (void)tm;

  const int rcx = pm_face_lcd_cx;
  const int rcy = pm_face_lcd_cy;
  const int rmax = 118;
  const uint16_t c_ring = pm_gfx->color565(48, 58, 72);
  const uint16_t c_spoke = pm_gfx->color565(64, 78, 96);
  const uint16_t c_sweep = pm_gfx->color565(72, 200, 140);
  const uint16_t c_self = pm_gfx->color565(120, 220, 255);
  const uint16_t c_label = pm_gfx->color565(150, 158, 170);
  const uint16_t c_peer = pm_gfx->color565(255, 196, 96);

  char title[24];
  if (valid) {
    snprintf(title, sizeof(title), "%02d:%02d", tm->tm_hour, tm->tm_min);
  } else {
    snprintf(title, sizeof(title), "--:--");
  }
  pm_face_draw_centered_line(title, 58, pm_gfx->color565(0x56, 0xd3, 0x64), 1, 1);
  pm_face_draw_centered_line("NEARBY", 78, c_label, 1, 1);

  for (int ring = 1; ring <= 5; ++ring) {
    const int rr = (rmax * ring) / 5;
    pm_gfx->drawCircle(rcx, rcy, rr, c_ring);
  }

  for (int i = 0; i < 12; ++i) {
    const float ang = i * (pm_face_k_two_pi / 12.f) - pm_face_k_pi * 0.5f;
    const int xe = rcx + static_cast<int>(lrintf(cosf(ang) * static_cast<float>(rmax)));
    const int ye = rcy + static_cast<int>(lrintf(sinf(ang) * static_cast<float>(rmax)));
    pm_gfx->drawLine(rcx, rcy, xe, ye, c_spoke);
  }

  const float sweep_rad = pm_face_deg_to_rad(s_sweep_deg);
  const int sx = rcx + static_cast<int>(lrintf(cosf(sweep_rad) * static_cast<float>(rmax)));
  const int sy = rcy + static_cast<int>(lrintf(sinf(sweep_rad) * static_cast<float>(rmax)));
  pm_gfx->drawLine(rcx, rcy, sx, sy, c_sweep);

  pm_gfx->fillCircle(rcx, rcy, 5, c_self);
  pm_gfx->drawCircle(rcx, rcy, 6, RGB565_WHITE);

  const size_t n = pm_presence_peer_count();
  for (size_t i = 0; i < n; ++i) {
    const PmPresencePeer *p = pm_presence_peer(i);
    if (!p) {
      continue;
    }
    const int ri = rssi_to_radius_px(p->rssi_ema, rmax);
    const float ang = pm_face_deg_to_rad(p->angle_deg);
    const int px = rcx + static_cast<int>(lrintf(cosf(ang) * static_cast<float>(ri)));
    const int py = rcy + static_cast<int>(lrintf(sinf(ang) * static_cast<float>(ri)));
    pm_gfx->fillCircle(px, py, 7, c_peer);
    pm_gfx->drawCircle(px, py, 8, RGB565_WHITE);

    char lab[8];
    snprintf(lab, sizeof(lab), "%04x", static_cast<unsigned>(p->device_id & 0xFFFFu));
    pm_face_draw_label_at_polar(rcx, rcy, ri + 14, ang, lab, c_label);
  }

  char footer[40];
  snprintf(footer, sizeof(footer), "%u peer%s  %s", static_cast<unsigned>(n), n == 1 ? "" : "s",
           pm_motion_has_gyro() ? "IMU" : "RSSI");
  pm_face_draw_centered_line(footer, 318, c_label, 1, 1);

  const char *ring_lbl[] = {"-40", "-55", "-70", "-85", "-95"};
  pm_gfx->setTextSize(1, 1);
  pm_gfx->setTextColor(c_label);
  for (int p = 0; p < 5; ++p) {
    const int step = (rmax * (5 - p)) / 5;
    int16_t x1, y1;
    uint16_t w, h;
    pm_gfx->getTextBounds(ring_lbl[p], 0, 0, &x1, &y1, &w, &h);
    pm_gfx->setCursor(rcx - 40 - static_cast<int>(w), rcy - step - static_cast<int>(h) / 2);
    pm_gfx->print(ring_lbl[p]);
  }
}
