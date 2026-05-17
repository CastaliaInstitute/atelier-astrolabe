#include "faces/calcifer/pm_face_calcifer.h"
#include "faces/shared/pm_circadian_hue.h"
#include "faces/shared/pm_face_draw.h"
#include "pm_calcifer.h"
#include "pm_wifi_ntp.h"
#include <cstdio>
#include <ctime>
#include "pin_config.h"
#include "pm_display.h"

PmCalciferStatus g_calcifer_ui = {};
bool s_calcifer_have_data = false;

namespace {

constexpr int64_t k_window_sec = 12 * 3600;

struct DaywheelEvent {
  int64_t start_unix = 0;
  int64_t end_unix = 0;
  const char *title = "";
  bool suggested = false;
};

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

float event_start_deg(int64_t start_unix, int64_t now_unix) {
  const float frac = static_cast<float>(start_unix - now_unix) / static_cast<float>(k_window_sec);
  if (frac <= 0.f) {
    return 0.f;
  }
  if (frac >= 1.f) {
    return 360.f;
  }
  return frac * 360.f;
}

float event_end_deg(int64_t end_unix, int64_t now_unix) {
  const float frac = static_cast<float>(end_unix - now_unix) / static_cast<float>(k_window_sec);
  if (frac <= 0.f) {
    return 0.f;
  }
  if (frac >= 1.f) {
    return 360.f;
  }
  return frac * 360.f;
}

enum class TemporalState { kPast, kCurrent, kFuture };

TemporalState event_state(int64_t start_unix, int64_t end_unix, int64_t now_unix) {
  if (end_unix <= now_unix) {
    return TemporalState::kPast;
  }
  if (start_unix <= now_unix && now_unix < end_unix) {
    return TemporalState::kCurrent;
  }
  return TemporalState::kFuture;
}

void push_event(DaywheelEvent *events, int *count, int cap, int64_t start, int64_t end, const char *title,
                bool suggested) {
  if (!title || !title[0] || *count >= cap) {
    return;
  }
  if (end <= start) {
    return;
  }
  events[*count].start_unix = start;
  events[*count].end_unix = end;
  events[*count].title = title;
  events[*count].suggested = suggested;
  (*count)++;
}

void add_demo_events(DaywheelEvent *events, int *count, int cap, int64_t now_unix) {
  const int64_t h = 3600;
  push_event(events, count, cap, now_unix + 30 * 60, now_unix + 60 * 60, "Breakfast", false);
  push_event(events, count, cap, now_unix + 90 * 60, now_unix + 150 * 60, "Outdoor Time", true);
  push_event(events, count, cap, now_unix + 7 * h, now_unix + 8 * h, "Faculty Conversation", false);
}

void draw_event_wedge(int cx, int cy, int r_inner, int r_outer, const DaywheelEvent &ev, int64_t now_unix) {
  const float a0 = event_start_deg(ev.start_unix, now_unix);
  const float a1 = event_end_deg(ev.end_unix, now_unix);
  if (a1 <= a0 + 0.5f) {
    return;
  }
  const int64_t mid = (ev.start_unix + ev.end_unix) / 2;
  const uint16_t hue_col = pm_circadian_color565_at_unix(static_cast<time_t>(mid));
  const TemporalState st = event_state(ev.start_unix, ev.end_unix, now_unix);
  float fill_alpha = ev.suggested ? 0.22f : 0.42f;
  if (st == TemporalState::kPast) {
    fill_alpha *= 0.45f;
  } else if (st == TemporalState::kCurrent) {
    fill_alpha = ev.suggested ? 0.35f : 0.58f;
  }
  const uint16_t c_fill = blend565(hue_col, pm_gfx->color565(240, 245, 255), fill_alpha);
  pm_face_draw_annular_wedge(cx, cy, r_inner, r_outer, a0, a1, c_fill);

  uint16_t stroke = pm_gfx->color565(200, 210, 230);
  if (st == TemporalState::kCurrent) {
    stroke = pm_gfx->color565(255, 250, 220);
  } else if (st == TemporalState::kPast) {
    stroke = pm_gfx->color565(90, 95, 110);
  }
  if (ev.suggested) {
    stroke = pm_gfx->color565(160, 175, 200);
  }
  const float ar0 = pm_face_deg_to_rad(a0);
  const float ar1 = pm_face_deg_to_rad(a1);
  const int x0 = cx + static_cast<int>(lrintf(cosf(ar0) * static_cast<float>(r_outer)));
  const int y0 = cy + static_cast<int>(lrintf(sinf(ar0) * static_cast<float>(r_outer)));
  const int x1 = cx + static_cast<int>(lrintf(cosf(ar1) * static_cast<float>(r_outer)));
  const int y1 = cy + static_cast<int>(lrintf(sinf(ar1) * static_cast<float>(r_outer)));
  pm_gfx->drawLine(x0, y0, x1, y1, stroke);
  if (st == TemporalState::kCurrent) {
    pm_gfx->drawLine(x0, y0, x1, y1, pm_gfx->color565(255, 255, 255));
  }
}

void draw_daywheel_geometry(int64_t now_unix) {
  const int cx = LCD_WIDTH / 2;
  const int cy = LCD_HEIGHT / 2;
  const int R = min(LCD_WIDTH, LCD_HEIGHT) / 2;
  const int r_outer_ring = R - 4;
  const int r_inner_ring = r_outer_ring - 10;
  const int r_evt_outer = r_inner_ring - 8;
  const int r_evt_inner = 94;
  const int r_inner_disk = 88;

  const uint16_t c_base = pm_circadian_color565_at_unix(static_cast<time_t>(now_unix));
  pm_gfx->fillScreen(blend565(c_base, pm_gfx->color565(0, 0, 0), 0.72f));

  pm_face_draw_daywheel_hue_ring_12h(now_unix, r_inner_ring, r_outer_ring);

  DaywheelEvent events[8] = {};
  int n_events = 0;
  if (g_calcifer_ui.current.valid) {
    push_event(events, &n_events, 8, g_calcifer_ui.current.start_unix, g_calcifer_ui.current.end_unix,
               g_calcifer_ui.current.summary, false);
  }
  if (g_calcifer_ui.next.valid) {
    bool dup = false;
    for (int i = 0; i < n_events; ++i) {
      if (events[i].start_unix == g_calcifer_ui.next.start_unix) {
        dup = true;
        break;
      }
    }
    if (!dup) {
      push_event(events, &n_events, 8, g_calcifer_ui.next.start_unix, g_calcifer_ui.next.end_unix,
                 g_calcifer_ui.next.summary, false);
    }
  }
  if (n_events == 0) {
    add_demo_events(events, &n_events, 8, now_unix);
  }

  for (int i = 0; i < n_events; ++i) {
    if (events[i].end_unix > now_unix && events[i].start_unix < now_unix + k_window_sec) {
      draw_event_wedge(cx, cy, r_evt_inner, r_evt_outer, events[i], now_unix);
    }
  }

  const uint16_t c_inner = blend565(c_base, pm_gfx->color565(255, 255, 255), 0.28f);
  pm_gfx->fillCircle(cx, cy, r_inner_disk, c_inner);
  pm_gfx->drawCircle(cx, cy, r_inner_disk, blend565(c_inner, pm_gfx->color565(255, 255, 255), 0.15f));

  for (int h = 1; h < 12; ++h) {
    const float deg = static_cast<float>(h) * 30.f;
    const float ang = pm_face_deg_to_rad(deg);
    const int x0 = cx + static_cast<int>(lrintf(cosf(ang) * static_cast<float>(r_inner_ring - 2)));
    const int y0 = cy + static_cast<int>(lrintf(sinf(ang) * static_cast<float>(r_inner_ring - 2)));
    const int x1 = cx + static_cast<int>(lrintf(cosf(ang) * static_cast<float>(r_outer_ring)));
    const int y1 = cy + static_cast<int>(lrintf(sinf(ang) * static_cast<float>(r_outer_ring)));
    pm_gfx->drawLine(x0, y0, x1, y1, pm_gfx->color565(255, 255, 255));
  }

  pm_face_draw_now_bead(cx, cy, r_evt_outer + 4, pm_gfx->color565(255, 250, 230));
}

}  // namespace

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

void pm_face_calcifer_format_until(int64_t start_unix, char *out, size_t cap) {
  const time_t now = time(nullptr);
  int64_t left = start_unix - static_cast<int64_t>(now);
  if (left < 0) {
    left = 0;
  }
  const int h = static_cast<int>(left / 3600);
  const int m = static_cast<int>((left % 3600) / 60);
  if (h > 0) {
    snprintf(out, cap, "in %dh %dm", h, m);
  } else if (m > 0) {
    snprintf(out, cap, "in %d min", m);
  } else {
    snprintf(out, cap, "soon");
  }
}

void pm_face_calcifer_draw() {
  const uint16_t c_label = pm_gfx->color565(200, 205, 220);
  const uint16_t c_big = RGB565_WHITE;
  const uint16_t c_dim = pm_gfx->color565(150, 158, 175);
  const uint16_t c_accent = pm_gfx->color565(255, 210, 130);

  if (!pm_time_valid()) {
    pm_gfx->fillScreen(pm_gfx->color565(12, 14, 24));
    pm_face_draw_centered_line("Daywheel", 200, c_accent, 1, 1);
    pm_face_draw_centered_line("need time", 232, c_dim, 2, 2);
    return;
  }

  const time_t now_sec = time(nullptr);
  const int64_t now_unix = static_cast<int64_t>(now_sec);
  draw_daywheel_geometry(now_unix);

  struct tm tm = {};
  pm_time_local(&tm);
  char time_line[16];
  const bool use_12h = true;
  if (use_12h) {
    int h = tm.tm_hour % 12;
    if (h == 0) {
      h = 12;
    }
    const char *ampm = (tm.tm_hour < 12) ? "AM" : "PM";
    snprintf(time_line, sizeof(time_line), "%d:%02d %s", h, tm.tm_min, ampm);
  } else {
    snprintf(time_line, sizeof(time_line), "%02d:%02d", tm.tm_hour, tm.tm_min);
  }

  if (!pm_wifi_connected()) {
    pm_face_draw_centered_line(pm_circadian_hue_name_now(), 188, c_accent, 1, 1);
    pm_face_draw_centered_line(time_line, 218, c_big, 2, 2);
    pm_face_draw_centered_line("need WiFi", 258, c_dim, 1, 1);
    return;
  }

  if (!s_calcifer_have_data) {
    pm_face_draw_centered_line(pm_circadian_hue_name_now(), 188, c_accent, 1, 1);
    pm_face_draw_centered_line(time_line, 218, c_big, 2, 2);
    pm_face_draw_centered_line("loading…", 258, c_dim, 1, 1);
    return;
  }

  if (!g_calcifer_ui.ok) {
    pm_face_draw_centered_line(pm_circadian_hue_name_now(), 188, c_accent, 1, 1);
    pm_face_draw_centered_line(time_line, 218, c_big, 2, 2);
    pm_face_draw_centered_line(g_calcifer_ui.error[0] ? g_calcifer_ui.error : "unavailable", 258,
                               pm_gfx->color565(255, 120, 110), 1, 1);
    return;
  }

  pm_face_draw_centered_line("RIGHT NOW", 168, c_label, 1, 1);
  pm_face_draw_centered_line(time_line, 196, c_big, 2, 2);
  pm_face_draw_centered_line(pm_circadian_hue_name_now(), 224, c_accent, 1, 1);

  char line[96];
  if (g_calcifer_ui.current.valid) {
    const int64_t left = g_calcifer_ui.current.end_unix - now_unix;
    const bool urgent = left > 0 && left < 300;
    pm_face_draw_centered_line("NOW", 248, c_label, 1, 1);
    pm_face_draw_centered_line(g_calcifer_ui.current.summary, 268, c_big, 1, 1);
    pm_face_calcifer_format_countdown(g_calcifer_ui.current.end_unix, line, sizeof(line));
    snprintf(line, sizeof(line), "until %s", line);
    pm_face_draw_centered_line(line, 292, urgent ? pm_gfx->color565(255, 110, 95) : c_dim, 1, 1);
  } else {
    pm_face_draw_centered_line("Open Time", 248, c_label, 1, 1);
    if (g_calcifer_ui.next.valid) {
      pm_face_draw_centered_line("NEXT", 268, c_label, 1, 1);
      pm_face_draw_centered_line(g_calcifer_ui.next.summary, 288, c_big, 1, 1);
      pm_face_calcifer_format_until(g_calcifer_ui.next.start_unix, line, sizeof(line));
      pm_face_draw_centered_line(line, 310, c_dim, 1, 1);
    } else if (!g_calcifer_ui.configured) {
      pm_face_draw_centered_line("demo rhythm", 288, c_dim, 1, 1);
    }
  }

  if (g_calcifer_ui.current.valid && g_calcifer_ui.next.valid) {
    const int64_t left = g_calcifer_ui.next.start_unix - now_unix;
    if (left > 0 && left < 3600) {
      pm_face_draw_centered_line("NEXT", 312, c_label, 1, 1);
      snprintf(line, sizeof(line), "%s", g_calcifer_ui.next.summary);
      pm_face_draw_centered_line(line, 330, c_dim, 1, 1);
      pm_face_calcifer_format_until(g_calcifer_ui.next.start_unix, line, sizeof(line));
      pm_face_draw_centered_line(line, 348, c_dim, 1, 1);
    }
  }
}
