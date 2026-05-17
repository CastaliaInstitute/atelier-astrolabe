#include "faces/rocket/pm_face_rocket.h"

#include "faces/shared/pm_face_draw.h"
#include "pm_display.h"
#include "pm_wifi_ntp.h"
#include "pin_config.h"
#include <cstdio>
#include <ctime>

PmRocketStatus g_rocket_ui = {};
bool s_rocket_have_data = false;

namespace {

constexpr int kCx = LCD_WIDTH / 2;
constexpr int kCy = 168;

void truncate_copy(const char *src, char *dst, size_t cap) {
  if (!dst || cap == 0) {
    return;
  }
  if (!src) {
    dst[0] = '\0';
    return;
  }
  strncpy(dst, src, cap - 1);
  dst[cap - 1] = '\0';
}

void draw_rocket_glyph(int cx, int cy, uint16_t body, uint16_t flame) {
  pm_gfx->fillTriangle(cx, cy - 44, cx - 18, cy + 10, cx + 18, cy + 10, body);
  pm_gfx->fillTriangle(cx, cy - 52, cx - 10, cy - 18, cx + 10, cy - 18, body);
  pm_gfx->fillRect(cx - 7, cy - 8, 14, 18, pm_gfx->color565(30, 40, 70));
  pm_gfx->fillTriangle(cx - 22, cy + 6, cx - 30, cy + 28, cx - 12, cy + 16, body);
  pm_gfx->fillTriangle(cx + 22, cy + 6, cx + 30, cy + 28, cx + 12, cy + 16, body);
  pm_gfx->fillTriangle(cx, cy + 12, cx - 12, cy + 34, cx + 12, cy + 34, flame);
  pm_gfx->fillTriangle(cx, cy + 22, cx - 7, cy + 42, cx + 7, cy + 42, pm_gfx->color565(255, 170, 60));
}

void draw_countdown_ring(int64_t net_unix, int64_t now_unix, uint16_t col) {
  const int64_t span = 7 * 24 * 3600;
  int64_t left = net_unix - now_unix;
  if (left < 0) {
    left = 0;
  }
  if (left > span) {
    left = span;
  }
  const float frac = 1.f - static_cast<float>(left) / static_cast<float>(span);
  const float sweep = frac * 300.f;
  if (sweep < 2.f) {
    return;
  }
  const int r0 = 198;
  const int r1 = 214;
  pm_face_draw_annular_wedge(kCx, kCy + 8, r0, r1, 240.f, 240.f + sweep, col);
}

}  // namespace

void pm_face_rocket_format_countdown(int64_t net_unix, char *out, size_t cap) {
  const time_t now = time(nullptr);
  int64_t left = net_unix - static_cast<int64_t>(now);
  if (left < 0) {
    left = 0;
  }
  const int64_t days = left / 86400;
  left %= 86400;
  const int h = static_cast<int>(left / 3600);
  const int m = static_cast<int>((left % 3600) / 60);
  const int s = static_cast<int>(left % 60);
  if (days > 0) {
    snprintf(out, cap, "T-%lldd %02d:%02d:%02d", static_cast<long long>(days), h, m, s);
  } else if (h > 0) {
    snprintf(out, cap, "T-%02d:%02d:%02d", h, m, s);
  } else {
    snprintf(out, cap, "T-%02d:%02d", m, s);
  }
}

void pm_face_rocket_format_until(int64_t net_unix, char *out, size_t cap) {
  const time_t now = time(nullptr);
  int64_t left = net_unix - static_cast<int64_t>(now);
  if (left < 0) {
    left = 0;
  }
  const int64_t days = left / 86400;
  left %= 86400;
  const int h = static_cast<int>(left / 3600);
  const int m = static_cast<int>((left % 3600) / 60);
  if (days > 0) {
    snprintf(out, cap, "in %lldd %dh", static_cast<long long>(days), h);
  } else if (h > 0) {
    snprintf(out, cap, "in %dh %dm", h, m);
  } else if (m > 0) {
    snprintf(out, cap, "in %d min", m);
  } else {
    snprintf(out, cap, "soon");
  }
}

void pm_face_rocket_draw() {
  const uint16_t c_label = pm_gfx->color565(170, 185, 210);
  const uint16_t c_big = RGB565_WHITE;
  const uint16_t c_dim = pm_gfx->color565(130, 140, 165);
  const uint16_t c_accent = pm_gfx->color565(120, 200, 255);
  const uint16_t c_flame = pm_gfx->color565(255, 120, 50);
  const uint16_t c_body = pm_gfx->color565(210, 220, 235);

  pm_gfx->fillScreen(pm_gfx->color565(8, 12, 28));

  if (!pm_time_valid()) {
    draw_rocket_glyph(kCx, kCy, c_body, c_flame);
    pm_face_draw_centered_line("ROCKET", 248, c_accent, 1, 1);
    pm_face_draw_centered_line("need time", 278, c_dim, 2, 2);
    return;
  }

  draw_rocket_glyph(kCx, kCy, c_body, c_flame);

  if (!pm_wifi_connected()) {
    pm_face_draw_centered_line("ROCKET", 248, c_accent, 1, 1);
    pm_face_draw_centered_line("need WiFi", 278, c_dim, 2, 2);
    return;
  }

  if (!s_rocket_have_data) {
    pm_face_draw_centered_line("ROCKET", 248, c_accent, 1, 1);
    pm_face_draw_centered_line("loading…", 278, c_dim, 2, 2);
    return;
  }

  if (!g_rocket_ui.ok || !g_rocket_ui.upcoming.valid) {
    pm_face_draw_centered_line("ROCKET", 248, c_accent, 1, 1);
    pm_face_draw_centered_line(g_rocket_ui.error[0] ? g_rocket_ui.error : "unavailable", 278,
                               pm_gfx->color565(255, 120, 110), 1, 1);
    return;
  }

  const PmRocketLaunch &lv = g_rocket_ui.upcoming;
  const int64_t now_unix = static_cast<int64_t>(time(nullptr));
  draw_countdown_ring(lv.net_unix, now_unix, pm_gfx->color565(70, 120, 200));

  char line[80];
  pm_face_rocket_format_countdown(lv.net_unix, line, sizeof(line));
  pm_face_draw_centered_line(line, 248, c_big, 2, 2);

  char title[40];
  truncate_copy(lv.name, title, sizeof(title));
  pm_face_draw_centered_line(title, 282, c_accent, 1, 1);

  if (lv.status_abbrev[0]) {
    snprintf(line, sizeof(line), "%s", lv.status_abbrev);
    pm_face_draw_centered_line(line, 306, c_label, 1, 1);
  }

  if (lv.vehicle[0]) {
    truncate_copy(lv.vehicle, title, sizeof(title));
    pm_face_draw_centered_line(title, 328, c_dim, 1, 1);
  }

  if (lv.pad[0]) {
    truncate_copy(lv.pad, title, sizeof(title));
    pm_face_draw_centered_line(title, 348, c_dim, 1, 1);
  } else if (lv.location[0]) {
    truncate_copy(lv.location, title, sizeof(title));
    pm_face_draw_centered_line(title, 348, c_dim, 1, 1);
  }

  pm_face_draw_centered_line("Launch Library 2", 372, pm_gfx->color565(90, 100, 125), 1, 1);
}
