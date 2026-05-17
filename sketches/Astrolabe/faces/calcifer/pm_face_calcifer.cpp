#include "faces/calcifer/pm_face_calcifer.h"
#include "faces/shared/pm_face_draw.h"
#include "pm_calcifer.h"
#include "pm_wifi_ntp.h"
#include <cstdio>
#include <ctime>
#include "pin_config.h"
#include "pm_display.h"

PmCalciferStatus g_calcifer_ui = {};
bool s_calcifer_have_data = false;
extern bool s_calcifer_have_data;

void pm_face_calcifer_format_countdown(int64_t end_unix, char *out, size_t cap) {
  const time_t now = time(nullptr);
  int64_t left = end_unix - static_cast<int64_t>(now);
  if (left < 0) {
    left = 0;
  }
  const int h = static_cast<int>(left / 3600);
  const int m = static_cast<int>((left % 3600) / 60);
  const int s = static_cast<int>(left % 60);
  if (h > 0) {
    snprintf(out, cap, "%d:%02d:%02d", h, m, s);
  } else {
    snprintf(out, cap, "%02d:%02d", m, s);
  }
}



void pm_face_calcifer_draw() {
  const uint16_t c_title = pm_gfx->color565(255, 190, 110);
  const uint16_t c_big = RGB565_WHITE;
  const uint16_t c_dim = pm_gfx->color565(130, 140, 155);
  pm_face_draw_centered_line("schedule", 52, c_title, 1, 1);

  if (!pm_wifi_connected()) {
    pm_face_draw_centered_line("need WiFi", 220, c_dim, 2, 2);
    return;
  }
  if (!pm_time_valid()) {
    pm_face_draw_centered_line("need time", 220, c_dim, 2, 2);
    return;
  }
  if (!s_calcifer_have_data) {
    pm_face_draw_centered_line("loading…", 220, c_dim, 2, 2);
    return;
  }
  if (!g_calcifer_ui.ok) {
    pm_face_draw_centered_line(g_calcifer_ui.error[0] ? g_calcifer_ui.error : "unavailable", 220,
                     pm_gfx->color565(255, 110, 110), 1, 1);
    return;
  }
  if (!g_calcifer_ui.configured) {
    pm_face_draw_centered_line("CalDAV not set", 210, c_dim, 1, 1);
    pm_face_draw_centered_line("on server", 240, c_dim, 1, 1);
    return;
  }

  char line[96];
  if (g_calcifer_ui.current.valid) {
    const time_t now_sec = time(nullptr);
    const int64_t left = g_calcifer_ui.current.end_unix - static_cast<int64_t>(now_sec);
    const bool urgent = left > 0 && left < 300;
    pm_face_draw_centered_line(g_calcifer_ui.current.summary, 150, c_dim, 1, 1);
    pm_face_calcifer_format_countdown(g_calcifer_ui.current.end_unix, line, sizeof(line));
    pm_face_draw_centered_line(line, 220, urgent ? pm_gfx->color565(255, 95, 85) : c_big, 3, 3);
    pm_face_draw_centered_line(urgent ? "ending soon" : "left in block", 286, urgent ? pm_gfx->color565(255, 150, 100) : c_dim, 1, 1);
  } else {
    pm_face_draw_centered_line("free", 200, c_big, 2, 2);
    if (g_calcifer_ui.next.valid) {
      snprintf(line, sizeof(line), "next: %s", g_calcifer_ui.next.summary);
      pm_face_draw_centered_line(line, 260, c_dim, 1, 1);
    }
  }
}


