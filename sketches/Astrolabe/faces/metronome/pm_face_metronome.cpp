#include "faces/metronome/pm_face_metronome.h"

#include "faces/shared/pm_face_draw.h"
#include <cmath>
#include <cstdio>
#include "pin_config.h"
#include "pm_display.h"

namespace {

constexpr int k_bpm_min = 40;
constexpr int k_bpm_max = 240;
constexpr int k_bpm_default = 120;
constexpr uint32_t k_repaint_interval_ms = 33;
constexpr uint32_t k_beat_flash_ms = 140;

int s_bpm = k_bpm_default;
bool s_running = false;
uint32_t s_next_beat_ms = 0;
uint32_t s_beat_flash_until = 0;
uint32_t s_last_repaint_ms = 0;

uint32_t beat_period_ms(void) {
  const int bpm = s_bpm < k_bpm_min ? k_bpm_min : (s_bpm > k_bpm_max ? k_bpm_max : s_bpm);
  return 60000u / static_cast<uint32_t>(bpm);
}

float pendulum_angle_rad(uint32_t now_ms) {
  constexpr float k_up = -pm_face_k_pi * 0.5f;
  if (!s_running) {
    return k_up;
  }
  const uint32_t period = beat_period_ms();
  if (period == 0) {
    return k_up;
  }
  const uint32_t beat_start = (s_next_beat_ms > period) ? (s_next_beat_ms - period) : 0;
  float phase = static_cast<float>(now_ms - beat_start) / static_cast<float>(period);
  if (phase < 0.f) {
    phase = 0.f;
  } else if (phase > 1.f) {
    phase = 1.f;
  }
  const float swing = sinf(phase * pm_face_k_pi);
  constexpr float k_max_swing = pm_face_k_pi * 0.24f;
  return k_up + swing * k_max_swing;
}

}  // namespace

void pm_face_metronome_on_face_leave(void) { s_running = false; }

void pm_face_metronome_toggle_running(void) {
  s_running = !s_running;
  const uint32_t now = millis();
  if (s_running) {
    s_next_beat_ms = now;
    s_beat_flash_until = now + k_beat_flash_ms;
  } else {
    s_next_beat_ms = 0;
  }
}

void pm_face_metronome_adjust_bpm(int delta) {
  int nb = s_bpm + delta;
  if (nb < k_bpm_min) {
    nb = k_bpm_min;
  } else if (nb > k_bpm_max) {
    nb = k_bpm_max;
  }
  s_bpm = nb;
  if (s_running) {
    s_next_beat_ms = millis() + beat_period_ms();
  }
}

bool pm_face_metronome_running(void) { return s_running; }

int pm_face_metronome_bpm(void) { return s_bpm; }

bool pm_face_metronome_tick(uint32_t now_ms) {
  if (!s_running) {
    return false;
  }
  if (s_next_beat_ms == 0) {
    s_next_beat_ms = now_ms;
    s_beat_flash_until = now_ms + k_beat_flash_ms;
    return true;
  }
  const uint32_t period = beat_period_ms();
  bool beat = false;
  while (now_ms >= s_next_beat_ms) {
    s_next_beat_ms += period;
    s_beat_flash_until = now_ms + k_beat_flash_ms;
    beat = true;
  }
  return beat;
}

bool pm_face_metronome_wants_repaint(uint32_t now_ms) {
  if (!s_running) {
    return now_ms < s_beat_flash_until;
  }
  if (now_ms < s_beat_flash_until) {
    return true;
  }
  if (s_last_repaint_ms == 0 || now_ms - s_last_repaint_ms >= k_repaint_interval_ms) {
    return true;
  }
  return false;
}

void pm_face_metronome_draw(uint32_t now_ms) {
  s_last_repaint_ms = now_ms;

  const int cx = LCD_WIDTH / 2;
  const int cy = LCD_HEIGHT / 2;
  const int R = min(LCD_WIDTH, LCD_HEIGHT) / 2 - 8;
  const uint16_t c_dim = pm_gfx->color565(120, 130, 150);
  const uint16_t c_accent = pm_gfx->color565(255, 200, 120);
  const uint16_t c_beat = pm_gfx->color565(255, 240, 200);
  const uint16_t c_arm = pm_gfx->color565(220, 175, 95);

  const float flash_t =
      (now_ms < s_beat_flash_until)
          ? 1.f - static_cast<float>(s_beat_flash_until - now_ms) / static_cast<float>(k_beat_flash_ms)
          : 0.f;

  pm_gfx->drawCircle(cx, cy, R - 52, c_dim);
  for (int i = 0; i < 12; ++i) {
    const float deg = static_cast<float>(i) * 30.f;
    const float ang = pm_face_deg_to_rad(deg);
    const int tick_r0 = R - 58;
    const int tick_r1 = R - 48;
    const bool major = (i % 3) == 0;
    pm_face_draw_radial_annulus_slice(cx, cy, ang, tick_r0, tick_r1,
                                      major ? c_dim : pm_gfx->color565(70, 78, 92), major ? 2 : 1);
  }

  if (flash_t > 0.05f) {
    const int half_w = static_cast<int>(4.f + flash_t * 10.f);
    pm_face_draw_radial_annulus_slice(cx, cy, -pm_face_k_pi * 0.5f, R - 72, R - 18, c_beat, half_w);
  }

  const float pend_ang = pendulum_angle_rad(now_ms);
  pm_face_draw_hand_radial(cx, cy, pend_ang, R - 70, c_arm, 3);
  pm_gfx->fillCircle(cx, cy, 6, c_accent);

  char line[16];
  snprintf(line, sizeof(line), "%d", s_bpm);
  pm_face_draw_centered_line(line, cy - 36, RGB565_WHITE, 5, 5);
  pm_face_draw_centered_line("BPM", cy + 18, c_dim, 2, 2);

  const char *status = s_running ? "running" : "tap to start";
  pm_face_draw_centered_line(status, cy + 52, s_running ? c_accent : c_dim, 1, 1);
  pm_face_draw_centered_line("swipe up/down tempo", 318, pm_gfx->color565(90, 98, 112), 1, 1);
}
