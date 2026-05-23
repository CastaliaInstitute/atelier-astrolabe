#include "faces/focus/pm_face_focus.h"

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <cstdio>
#include <cmath>

#include "faces/shared/pm_face_draw.h"
#include "pin_config.h"
#include "pm_display.h"

namespace {

struct FocusMode {
  const char *label;
  uint32_t seconds;
  uint8_t r;
  uint8_t g;
  uint8_t b;
};

constexpr FocusMode k_modes[] = {
    {"focus", 25u * 60u, 255, 92, 80},
    {"break", 5u * 60u, 96, 210, 160},
    {"long", 15u * 60u, 112, 168, 255},
};

static int s_mode = 0;
static bool s_running = false;
static bool s_finished = false;
static uint32_t s_started_ms = 0;
static uint32_t s_elapsed_before_ms = 0;

uint32_t duration_ms() {
  return k_modes[s_mode].seconds * 1000u;
}

uint32_t elapsed_ms() {
  uint32_t elapsed = s_elapsed_before_ms;
  if (s_running) {
    elapsed += millis() - s_started_ms;
  }
  const uint32_t dur = duration_ms();
  return elapsed > dur ? dur : elapsed;
}

uint32_t remaining_seconds() {
  const uint32_t dur = duration_ms();
  const uint32_t elapsed = elapsed_ms();
  if (elapsed >= dur) {
    return 0;
  }
  return (dur - elapsed + 999u) / 1000u;
}

void draw_progress_arc(float progress, uint16_t accent, uint16_t track) {
  constexpr int kR = 178;
  constexpr int kHalfW = 5;
  constexpr int kSegments = 180;
  for (int i = 0; i < kSegments; ++i) {
    const float deg = (static_cast<float>(i) / static_cast<float>(kSegments)) * 360.0f;
    pm_face_draw_radial_annulus_slice(pm_face_lcd_cx, pm_face_lcd_cy, pm_face_deg_to_rad(deg),
                                      kR - kHalfW, kR + kHalfW, track, 2);
  }
  const int filled = static_cast<int>(progress * static_cast<float>(kSegments) + 0.5f);
  for (int i = 0; i < filled; ++i) {
    const float deg = (static_cast<float>(i) / static_cast<float>(kSegments)) * 360.0f;
    pm_face_draw_radial_annulus_slice(pm_face_lcd_cx, pm_face_lcd_cy, pm_face_deg_to_rad(deg),
                                      kR - kHalfW, kR + kHalfW, accent, 3);
  }
}

}  // namespace

bool pm_face_focus_toggle() {
  if (s_finished) {
    pm_face_focus_reset();
  }
  if (s_running) {
    s_elapsed_before_ms = elapsed_ms();
    s_running = false;
  } else {
    s_started_ms = millis();
    s_running = true;
  }
  return s_running;
}

void pm_face_focus_reset() {
  s_running = false;
  s_finished = false;
  s_started_ms = 0;
  s_elapsed_before_ms = 0;
}

int pm_face_focus_cycle_mode(int delta) {
  constexpr int n = static_cast<int>(sizeof(k_modes) / sizeof(k_modes[0]));
  s_mode = (s_mode + delta + n) % n;
  pm_face_focus_reset();
  return s_mode;
}

bool pm_face_focus_running() {
  return s_running;
}

const char *pm_face_focus_mode_label() {
  return k_modes[s_mode].label;
}

void pm_face_focus_draw() {
  const FocusMode &mode = k_modes[s_mode];
  const uint16_t bg = pm_gfx->color565(10, 12, 15);
  const uint16_t panel = pm_gfx->color565(20, 24, 28);
  const uint16_t dim = pm_gfx->color565(96, 104, 110);
  const uint16_t text = pm_gfx->color565(238, 242, 236);
  const uint16_t accent = pm_gfx->color565(mode.r, mode.g, mode.b);
  const uint16_t track = pm_gfx->color565(42, 48, 52);

  const uint32_t elapsed = elapsed_ms();
  const uint32_t dur = duration_ms();
  if (elapsed >= dur && !s_finished) {
    s_running = false;
    s_finished = true;
    s_elapsed_before_ms = dur;
  }

  pm_gfx->fillScreen(bg);
  pm_gfx->fillCircle(pm_face_lcd_cx, pm_face_lcd_cy, 164, panel);

  const float progress = dur == 0 ? 0.f : static_cast<float>(elapsed) / static_cast<float>(dur);
  draw_progress_arc(progress, accent, track);

  char mmss[16];
  const uint32_t remain = remaining_seconds();
  snprintf(mmss, sizeof(mmss), "%02u:%02u", static_cast<unsigned>(remain / 60u),
           static_cast<unsigned>(remain % 60u));

  pm_face_draw_centered_line(mode.label, 118, accent, 2, 2);
  pm_face_draw_centered_line(mmss, 198, text, 5, 5);

  const char *state = "tap start";
  if (s_finished) {
    state = "complete";
  } else if (s_running) {
    state = "tap pause";
  } else if (s_elapsed_before_ms > 0) {
    state = "tap resume";
  }
  pm_face_draw_centered_line(state, 292, dim, 2, 2);

  char cycle[28];
  snprintf(cycle, sizeof(cycle), "%u min  swipe mode", static_cast<unsigned>(mode.seconds / 60u));
  pm_face_draw_centered_line(cycle, 334, dim, 1, 1);

  if (s_finished) {
    const uint32_t pulse = (millis() / 280u) & 1u;
    pm_gfx->drawCircle(pm_face_lcd_cx, pm_face_lcd_cy, pulse ? 152 : 146, accent);
    pm_gfx->drawCircle(pm_face_lcd_cx, pm_face_lcd_cy, pulse ? 156 : 150, accent);
  }
}
