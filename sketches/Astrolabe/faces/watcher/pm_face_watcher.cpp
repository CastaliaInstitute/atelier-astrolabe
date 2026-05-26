#include "faces/watcher/pm_face_watcher.h"

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <cmath>
#include <cstdio>

#include "faces/shared/pm_face_draw.h"
#include "pm_castalia_auth.h"
#include "pm_display.h"
#include "pm_heap.h"
#include "pm_wifi_ntp.h"

namespace {

constexpr const char *kModeLabels[] = {"camera", "settle", "metrics", "voice"};
int s_mode = 0;
uint32_t s_entered_ms = 0;

uint16_t mix(uint8_t r, uint8_t g, uint8_t b) {
  return pm_gfx->color565(r, g, b);
}

void draw_arc(float start_deg, float end_deg, int r, int half_w, uint16_t col) {
  const int segments = 80;
  for (int i = 0; i <= segments; ++i) {
    const float t = static_cast<float>(i) / static_cast<float>(segments);
    const float deg = start_deg + (end_deg - start_deg) * t;
    pm_face_draw_radial_annulus_slice(pm_face_lcd_cx, pm_face_lcd_cy, pm_face_deg_to_rad(deg),
                                      r - half_w, r + half_w, col, 2);
  }
}

void draw_bezel_labels(uint16_t dim, uint16_t accent) {
  pm_face_draw_label_at_polar(pm_face_lcd_cx, pm_face_lcd_cy, 184, pm_face_deg_to_rad(0.f), "ASK", accent);
  pm_face_draw_label_at_polar(pm_face_lcd_cx, pm_face_lcd_cy, 184, pm_face_deg_to_rad(60.f), "FACE", dim);
  pm_face_draw_label_at_polar(pm_face_lcd_cx, pm_face_lcd_cy, 184, pm_face_deg_to_rad(120.f), "AUTO", dim);
  pm_face_draw_label_at_polar(pm_face_lcd_cx, pm_face_lcd_cy, 184, pm_face_deg_to_rad(180.f), "BOX", dim);
  pm_face_draw_label_at_polar(pm_face_lcd_cx, pm_face_lcd_cy, 184, pm_face_deg_to_rad(240.f), "RGB", dim);
  pm_face_draw_label_at_polar(pm_face_lcd_cx, pm_face_lcd_cy, 184, pm_face_deg_to_rad(300.f), "CLOSE", dim);
}

void draw_camera_frame(uint16_t frame, uint16_t dim, uint16_t accent) {
  constexpr int left = 86;
  constexpr int top = 58;
  constexpr int size = 294;
  pm_gfx->drawRect(left, top, size, size, frame);
  pm_gfx->drawRect(left + 1, top + 1, size - 2, size - 2, frame);

  for (int y = top + 18; y < top + size - 18; y += 22) {
    pm_gfx->drawFastHLine(left + 16, y, size - 32, dim);
  }
  for (int x = left + 18; x < left + size - 18; x += 22) {
    pm_gfx->drawFastVLine(x, top + 16, size - 32, dim);
  }

  const float breath = (sinf(static_cast<float>(millis() - s_entered_ms) * 0.004f) + 1.f) * 0.5f;
  const int box_w = 92 + static_cast<int>(breath * 10.f);
  const int box_h = 118 + static_cast<int>(breath * 8.f);
  const int box_x = pm_face_lcd_cx - box_w / 2;
  const int box_y = 136 - static_cast<int>(breath * 4.f);
  pm_gfx->drawRect(box_x, box_y, box_w, box_h, accent);
  pm_gfx->drawRect(box_x + 1, box_y + 1, box_w - 2, box_h - 2, accent);
}

}  // namespace

void pm_face_watcher_on_enter(void) {
  s_entered_ms = millis();
}

void pm_face_watcher_on_leave(void) {}

const char *pm_face_watcher_cycle_mode(void) {
  constexpr int n = static_cast<int>(sizeof(kModeLabels) / sizeof(kModeLabels[0]));
  s_mode = (s_mode + 1) % n;
  return kModeLabels[s_mode];
}

const char *pm_face_watcher_mode_label(void) {
  return kModeLabels[s_mode];
}

void pm_face_watcher_format_prompt_state(char *out, size_t cap) {
  if (!out || cap == 0) {
    return;
  }
  snprintf(out, cap,
           "mode=%s; WiFi=%s; Castalia session=%s; heap_largest=%u; Watcher camera path uses SSCMA "
           "presence boxes, center-and-settle gating, face.castalia.institute metrics, then Castalia LLM-TTS",
           pm_face_watcher_mode_label(), pm_wifi_connected() ? "connected" : "offline",
           pm_castalia_has_session() ? "signed-in" : "not-signed-in",
           static_cast<unsigned>(pm_heap_internal_largest()));
}

void pm_face_watcher_draw(void) {
  const uint16_t bg = mix(5, 8, 10);
  const uint16_t panel = mix(12, 18, 20);
  const uint16_t dim = mix(52, 86, 86);
  const uint16_t text = mix(218, 235, 226);
  const uint16_t accent = mix(110, 232, 190);
  const uint16_t castalia = mix(156, 86, 220);
  const uint16_t warn = mix(255, 120, 98);

  pm_gfx->fillScreen(bg);
  pm_gfx->fillCircle(pm_face_lcd_cx, pm_face_lcd_cy, 205, panel);
  pm_gfx->drawCircle(pm_face_lcd_cx, pm_face_lcd_cy, 210, dim);
  pm_gfx->drawCircle(pm_face_lcd_cx, pm_face_lcd_cy, 214, accent);

  draw_bezel_labels(dim, accent);
  draw_camera_frame(dim, mix(22, 42, 42), accent);

  const bool wifi = pm_wifi_connected();
  const bool session = pm_castalia_has_session();
  const uint16_t status = wifi && session ? accent : warn;
  const float phase = fmodf(static_cast<float>(millis() - s_entered_ms) * 0.045f, 360.f);
  draw_arc(phase, phase + 54.f, 198, 4, status);
  draw_arc(phase + 128.f, phase + 182.f, 198, 4, castalia);

  pm_face_draw_centered_line("WATCHER", 78, text, 2, 2);
  pm_face_draw_centered_line(kModeLabels[s_mode], 356, accent, 2, 2);

  char state[48];
  snprintf(state, sizeof(state), "%s  %s", wifi ? "wifi" : "offline", session ? "castalia" : "pair");
  pm_face_draw_centered_line(state, 388, wifi && session ? dim : warn, 1, 1);
}
