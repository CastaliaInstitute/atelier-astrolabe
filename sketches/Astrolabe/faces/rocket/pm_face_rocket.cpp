#include "faces/rocket/pm_face_rocket.h"

#include "faces/shared/pm_face_draw.h"
#include "pm_display.h"
#include "pm_geo_tz.h"
#include "pm_wifi_ntp.h"
#include "pin_config.h"
#include <cmath>
#include <cstdio>
#include <ctime>

PmRocketStatus g_rocket_ui = {};
bool s_rocket_have_data = false;

namespace {

constexpr int64_t k_window_sec = 14 * 24 * 3600;

constexpr int kCx = LCD_WIDTH / 2;
constexpr int kCy = LCD_HEIGHT / 2 - 12;

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

void short_mission_label(const PmRocketLaunch &lv, char *out, size_t cap) {
  const char *pipe = strchr(lv.name, '|');
  if (pipe) {
    while (*pipe == ' ' || *pipe == '|') {
      ++pipe;
    }
    if (*pipe) {
      truncate_copy(pipe, out, cap);
      return;
    }
  }
  if (lv.vehicle[0]) {
    truncate_copy(lv.vehicle, out, cap);
    return;
  }
  truncate_copy(lv.name, out, cap);
}

void format_launch_local(int64_t net_unix, char *out, size_t cap) {
  static const char *kDow[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  struct tm tm = {};
  const time_t local = static_cast<time_t>(net_unix) + pm_geo_tz_offset_sec();
  gmtime_r(&local, &tm);
  const char *dow = kDow[tm.tm_wday % 7];
  if (tm.tm_mday <= 0) {
    snprintf(out, cap, "%s --:--", dow);
    return;
  }
  int h = tm.tm_hour % 12;
  if (h == 0) {
    h = 12;
  }
  const char *ampm = (tm.tm_hour < 12) ? "a" : "p";
  snprintf(out, cap, "%s %d:%02d%s", dow, h, tm.tm_min, ampm);
}

float launch_deg(int64_t net_unix, int64_t now_unix) {
  const float frac = static_cast<float>(net_unix - now_unix) / static_cast<float>(k_window_sec);
  if (frac <= 0.f) {
    return 0.f;
  }
  if (frac >= 1.f) {
    return 360.f;
  }
  return frac * 360.f;
}

uint16_t blend565(uint16_t bg, uint16_t fg, float alpha) {
  if (alpha <= 0.f) {
    return bg;
  }
  if (alpha >= 1.f) {
    return fg;
  }
  const uint8_t br = static_cast<uint8_t>(((bg >> 11) & 0x1F) * 255 / 31);
  const uint8_t bg_g = static_cast<uint8_t>(((bg >> 5) & 0x3F) * 255 / 63);
  const uint8_t bb = static_cast<uint8_t>((bg & 0x1F) * 255 / 31);
  const uint8_t fr = static_cast<uint8_t>(((fg >> 11) & 0x1F) * 255 / 31);
  const uint8_t fg_g = static_cast<uint8_t>(((fg >> 5) & 0x3F) * 255 / 63);
  const uint8_t fb = static_cast<uint8_t>((fg & 0x1F) * 255 / 31);
  const float a = alpha;
  const float ia = 1.f - a;
  return pm_gfx->color565(static_cast<uint8_t>(br * ia + fr * a), static_cast<uint8_t>(bg_g * ia + fg_g * a),
                          static_cast<uint8_t>(bb * ia + fb * a));
}

void draw_launch_tick(int cx, int cy, int r, float deg, uint16_t col, int size) {
  const float ang = pm_face_deg_to_rad(deg);
  const int x = cx + static_cast<int>(lrintf(cosf(ang) * static_cast<float>(r)));
  const int y = cy + static_cast<int>(lrintf(sinf(ang) * static_cast<float>(r)));
  pm_gfx->fillTriangle(x, y - size, x - size, y + size, x + size, y + size, col);
}

void draw_launch_clock_dial(int64_t now_unix, const PmRocketStatus &ui) {
  const int R = min(LCD_WIDTH, LCD_HEIGHT) / 2;
  const int r_outer = R - 6;
  const int r_inner = r_outer - 14;
  const int r_markers = r_inner - 10;
  const int r_disk = 92;

  const uint16_t c_bg = pm_gfx->color565(6, 10, 24);
  const uint16_t c_ring = pm_gfx->color565(40, 55, 90);
  const uint16_t c_ring_hi = pm_gfx->color565(80, 110, 160);
  pm_gfx->fillScreen(c_bg);
  pm_gfx->drawCircle(kCx, kCy, r_outer, c_ring_hi);
  pm_gfx->drawCircle(kCx, kCy, r_inner, c_ring);

  for (int d = 1; d < 14; ++d) {
    const float deg = static_cast<float>(d) * (360.f / 14.f);
    const float ang = pm_face_deg_to_rad(deg);
    const int x0 = kCx + static_cast<int>(lrintf(cosf(ang) * static_cast<float>(r_inner)));
    const int y0 = kCy + static_cast<int>(lrintf(sinf(ang) * static_cast<float>(r_inner)));
    const int x1 = kCx + static_cast<int>(lrintf(cosf(ang) * static_cast<float>(r_outer)));
    const int y1 = kCy + static_cast<int>(lrintf(sinf(ang) * static_cast<float>(r_outer)));
    pm_gfx->drawLine(x0, y0, x1, y1, pm_gfx->color565(55, 70, 100));
  }

  const uint16_t c_inner = blend565(c_bg, pm_gfx->color565(30, 45, 80), 0.55f);
  pm_gfx->fillCircle(kCx, kCy, r_disk, c_inner);
  pm_gfx->drawCircle(kCx, kCy, r_disk, pm_gfx->color565(90, 120, 170));

  for (int i = 0; i < ui.count; ++i) {
    const PmRocketLaunch &lv = ui.launches[i];
    if (!lv.valid) {
      continue;
    }
    const float deg = launch_deg(lv.net_unix, now_unix);
    const bool is_next = (i == 0);
    const uint16_t col =
        is_next ? pm_gfx->color565(255, 150, 70) : pm_gfx->color565(120, 180, 255);
    draw_launch_tick(kCx, kCy, r_markers, deg, col, is_next ? 7 : 5);
  }

  pm_face_draw_now_bead(kCx, kCy - r_markers, 5, pm_gfx->color565(255, 250, 230));
}

void draw_center_clock(const PmRocketLaunch *next) {
  struct tm tm = {};
  pm_time_local(&tm);
  char time_line[16];
  int h = tm.tm_hour % 12;
  if (h == 0) {
    h = 12;
  }
  const char *ampm = (tm.tm_hour < 12) ? "AM" : "PM";
  snprintf(time_line, sizeof(time_line), "%d:%02d %s", h, tm.tm_min, ampm);

  const uint16_t c_big = RGB565_WHITE;
  const uint16_t c_accent = pm_gfx->color565(130, 200, 255);
  const uint16_t c_dim = pm_gfx->color565(150, 165, 190);

  pm_face_draw_centered_line(time_line, kCy - 8, c_big, 2, 2);
  pm_face_draw_centered_line("LAUNCH CLOCK", kCy + 18, c_accent, 1, 1);

  if (next) {
    char line[48];
    pm_face_rocket_format_countdown(next->net_unix, line, sizeof(line));
    pm_face_draw_centered_line(line, kCy + 38, c_big, 1, 1);
    if (next->status_abbrev[0]) {
      snprintf(line, sizeof(line), "%s", next->status_abbrev);
      pm_face_draw_centered_line(line, kCy + 56, c_dim, 1, 1);
    }
  }
}

void draw_upcoming_list(const PmRocketStatus &ui) {
  const uint16_t c_time = pm_gfx->color565(180, 195, 220);
  const uint16_t c_name = pm_gfx->color565(220, 230, 245);
  const uint16_t c_label = pm_gfx->color565(110, 125, 150);
  int y = 318;
  pm_face_draw_centered_line("UPCOMING", y, c_label, 1, 1);
  y += 20;

  const int rows = ui.count < 3 ? ui.count : 3;
  for (int i = 0; i < rows; ++i) {
    const PmRocketLaunch &lv = ui.launches[i];
    char when[16];
    char mission[28];
    format_launch_local(lv.net_unix, when, sizeof(when));
    short_mission_label(lv, mission, sizeof(mission));
    char line[44];
    snprintf(line, sizeof(line), "%s  %s", when, mission);
    pm_face_draw_centered_line(line, y, i == 0 ? c_name : c_time, 1, 1);
    y += 18;
  }
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
  const uint16_t c_dim = pm_gfx->color565(130, 140, 165);
  const uint16_t c_accent = pm_gfx->color565(120, 200, 255);

  if (!pm_time_valid()) {
    pm_gfx->fillScreen(pm_gfx->color565(8, 12, 28));
    pm_face_draw_centered_line("LAUNCH CLOCK", 220, c_accent, 1, 1);
    pm_face_draw_centered_line("need time", 252, c_dim, 2, 2);
    return;
  }

  const int64_t now_unix = static_cast<int64_t>(time(nullptr));

  if (!pm_wifi_connected()) {
    draw_launch_clock_dial(now_unix, g_rocket_ui);
    draw_center_clock(nullptr);
    pm_face_draw_centered_line("need WiFi", 292, c_dim, 1, 1);
    return;
  }

  if (!s_rocket_have_data) {
    draw_launch_clock_dial(now_unix, g_rocket_ui);
    draw_center_clock(nullptr);
    pm_face_draw_centered_line("loading…", 292, c_dim, 1, 1);
    return;
  }

  if (!g_rocket_ui.ok || g_rocket_ui.count <= 0) {
    draw_launch_clock_dial(now_unix, g_rocket_ui);
    draw_center_clock(nullptr);
    pm_face_draw_centered_line(g_rocket_ui.error[0] ? g_rocket_ui.error : "unavailable", 292,
                               pm_gfx->color565(255, 120, 110), 1, 1);
    return;
  }

  draw_launch_clock_dial(now_unix, g_rocket_ui);
  draw_center_clock(pm_rocket_next(&g_rocket_ui));
  draw_upcoming_list(g_rocket_ui);
}
