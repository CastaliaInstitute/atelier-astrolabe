#include "faces/rocket/pm_face_rocket.h"

#include "faces/shared/pm_face_draw.h"
#include "pm_display.h"
#include "pm_geo_tz.h"
#include "pm_wifi_ntp.h"
#include "pin_config.h"
#include "third_party/qrcodegen/qrcodegen.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>

PmRocketStatus g_rocket_ui = {};
bool s_rocket_have_data = false;
static int s_rocket_selected_idx = 0;
static int s_timeline_selected_idx = -1;
static uint32_t s_timeline_selected_until_ms = 0;

namespace {

constexpr int kStreamQrMaxVersion = 10;
constexpr size_t kStreamQrBufLen = qrcodegen_BUFFER_LEN_FOR_VERSION(kStreamQrMaxVersion);
static uint8_t s_stream_qr_temp[kStreamQrBufLen];
static uint8_t s_stream_qr_out[kStreamQrBufLen];
static char s_stream_qr_cached_url[192] = "";
static bool s_stream_qr_modules_valid = false;
static int s_stream_qr_cached_size = 0;
static bool s_stream_qr_visible = false;
static uint32_t s_stream_last_repaint_ms = 0;
static uint32_t s_clock_last_repaint_ms = 0;

/** Half the launch ring: top = T-0; left = T-, right = T+ (12h each side). */
constexpr int64_t k_half_window_sec = 12 * 3600;
constexpr int32_t k_timeline_pre_sec = 60 * 60;
constexpr int32_t k_timeline_post_sec = 70 * 60;

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

/** Degrees clockwise from top; 0 = T-0 (launch / “midnight”). */
float t_rel_deg(int64_t event_unix, int64_t launch_unix) {
  const float delta = static_cast<float>(event_unix - launch_unix);
  const float half = static_cast<float>(k_half_window_sec);
  float deg = (delta / half) * 180.f;
  if (deg < -180.f) {
    deg = -180.f;
  } else if (deg > 180.f) {
    deg = 180.f;
  }
  return deg;
}

float timeline_offset_deg(int64_t offset_sec, bool timeline_mode) {
  if (!timeline_mode) {
    float deg = (static_cast<float>(offset_sec) / static_cast<float>(k_half_window_sec)) * 180.f;
    if (deg < -180.f) return -180.f;
    if (deg > 180.f) return 180.f;
    return deg;
  }
  if (offset_sec < 0) {
    float deg = (static_cast<float>(offset_sec) / static_cast<float>(k_timeline_pre_sec)) * 180.f;
    return deg < -180.f ? -180.f : deg;
  }
  float deg = (static_cast<float>(offset_sec) / static_cast<float>(k_timeline_post_sec)) * 180.f;
  return deg > 180.f ? 180.f : deg;
}

float normalize_deg180(float deg) {
  while (deg > 180.f) {
    deg -= 360.f;
  }
  while (deg < -180.f) {
    deg += 360.f;
  }
  return deg;
}

void format_timeline_offset(int32_t offset_sec, char *out, size_t cap) {
  if (!out || cap == 0) {
    return;
  }
  const bool after = offset_sec >= 0;
  int32_t v = after ? offset_sec : -offset_sec;
  const int m = v / 60;
  const int s = v % 60;
  snprintf(out, cap, after ? "T+%02d:%02d" : "T-%02d:%02d", m, s);
}

bool within_launch_ring(int64_t event_unix, int64_t launch_unix) {
  const int64_t d = event_unix - launch_unix;
  return d >= -k_half_window_sec && d <= k_half_window_sec;
}

int selected_launch_index(const PmRocketStatus &ui) {
  if (!ui.ok || ui.count <= 0) {
    return 0;
  }
  if (s_rocket_selected_idx < 0) {
    s_rocket_selected_idx = 0;
  } else if (s_rocket_selected_idx >= ui.count) {
    s_rocket_selected_idx = ui.count - 1;
  }
  if (!ui.launches[s_rocket_selected_idx].valid) {
    for (int i = 0; i < ui.count; ++i) {
      if (ui.launches[i].valid) {
        s_rocket_selected_idx = i;
        break;
      }
    }
  }
  return s_rocket_selected_idx;
}

const PmRocketLaunch *selected_launch(const PmRocketStatus &ui) {
  const int idx = selected_launch_index(ui);
  return ui.count > 0 && idx >= 0 && idx < ui.count && ui.launches[idx].valid ? &ui.launches[idx] : nullptr;
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

void draw_ring_tick(int cx, int cy, int r0, int r1, float deg, uint16_t col) {
  const float ang = pm_face_deg_to_rad(deg);
  const int x0 = cx + static_cast<int>(lrintf(cosf(ang) * static_cast<float>(r0)));
  const int y0 = cy + static_cast<int>(lrintf(sinf(ang) * static_cast<float>(r0)));
  const int x1 = cx + static_cast<int>(lrintf(cosf(ang) * static_cast<float>(r1)));
  const int y1 = cy + static_cast<int>(lrintf(sinf(ang) * static_cast<float>(r1)));
  pm_gfx->drawLine(x0, y0, x1, y1, col);
}

void draw_now_bead_at(int cx, int cy, int r, float deg, uint16_t col) {
  const float ang = pm_face_deg_to_rad(deg);
  const int x = cx + static_cast<int>(lrintf(cosf(ang) * static_cast<float>(r)));
  const int y = cy + static_cast<int>(lrintf(sinf(ang) * static_cast<float>(r)));
  pm_gfx->fillCircle(x, y, 5, col);
  pm_gfx->drawCircle(x, y, 6, pm_gfx->color565(255, 255, 255));
}

void draw_timeline_events(const PmRocketStatus &ui) {
  if (ui.timeline_count <= 0) {
    return;
  }
  const int R = min(LCD_WIDTH, LCD_HEIGHT) / 2;
  const int r0 = R - 28;
  const int r1 = R - 5;
  const int r_label_pre = R - 48;
  const int r_label_post = R - 24;
  const uint16_t c_pre = pm_gfx->color565(105, 165, 240);
  const uint16_t c_post = pm_gfx->color565(245, 150, 85);
  const uint16_t c_launch = pm_gfx->color565(255, 220, 135);
  for (int i = 0; i < ui.timeline_count; ++i) {
    const PmRocketTimelineEvent &e = ui.timeline[i];
    if (!e.valid) {
      continue;
    }
    const float deg = timeline_offset_deg(e.offset_sec, true);
    const bool launch = e.offset_sec == 0;
    uint16_t col = launch ? c_launch : (e.offset_sec < 0 ? c_pre : c_post);
    const bool selected = i == s_timeline_selected_idx && millis() < s_timeline_selected_until_ms;
    if (selected) {
      col = pm_gfx->color565(255, 255, 245);
    }
    draw_ring_tick(kCx, kCy, selected ? r0 - 5 : r0, r1, deg, col);

    const bool major = strstr(e.label, "Entry") ||
                       strcmp(e.label, "Landing burn start") == 0 || strcmp(e.label, "An exciting landing!") == 0;
    if (major) {
      char lbl[18];
      truncate_copy(e.label, lbl, sizeof(lbl));
      pm_face_draw_label_at_polar(kCx, kCy, e.offset_sec < 0 ? r_label_pre : r_label_post,
                                  pm_face_deg_to_rad(deg), lbl, col);
    }
  }
}

void draw_selected_timeline_label(const PmRocketStatus &ui) {
  if (s_timeline_selected_idx < 0 || s_timeline_selected_idx >= ui.timeline_count ||
      millis() >= s_timeline_selected_until_ms) {
    return;
  }
  const PmRocketTimelineEvent &e = ui.timeline[s_timeline_selected_idx];
  if (!e.valid) {
    return;
  }
  char t[16];
  format_timeline_offset(e.offset_sec, t, sizeof(t));
  char label[52];
  snprintf(label, sizeof(label), "%s  %.32s", t, e.label);
  const int w = 210;
  const int h = 34;
  const int x = kCx - w / 2;
  const int y = 54;
  pm_gfx->fillRoundRect(x, y, w, h, 5, pm_gfx->color565(248, 248, 240));
  pm_gfx->drawRoundRect(x, y, w, h, 5, pm_gfx->color565(255, 210, 125));
  pm_face_draw_centered_line(label, y + 10, pm_gfx->color565(10, 14, 22), 1, 1);
}

void draw_launch_clock_dial(int64_t now_unix, const PmRocketStatus &ui) {
  const int R = min(LCD_WIDTH, LCD_HEIGHT) / 2;
  const int r_outer = R - 6;
  const int r_inner = r_outer - 14;
  const int r_arc_inner = r_inner - 4;
  const int r_arc_outer = r_inner - 1;
  const int r_markers = r_arc_inner - 10;
  const int r_disk = 92;

  const uint16_t c_bg = pm_gfx->color565(6, 10, 24);
  const int cover_r = min(LCD_WIDTH, LCD_HEIGHT) / 2;
  const uint16_t c_ring = pm_gfx->color565(40, 55, 90);
  const uint16_t c_ring_hi = pm_gfx->color565(80, 110, 160);
  const uint16_t c_tminus = pm_gfx->color565(70, 120, 200);
  const uint16_t c_tplus = pm_gfx->color565(200, 110, 60);
  const uint16_t c_launch = pm_gfx->color565(255, 200, 120);

  pm_gfx->fillScreen(c_bg);
  if (pm_rocket_pad_image_ready()) {
    pm_rocket_pad_image_draw_background(kCx, kCy + 12, cover_r, c_bg, 0.62f);
  }

  const int selected_idx = selected_launch_index(ui);
  const PmRocketLaunch *primary = selected_launch(ui);
  const int64_t t0 = primary ? primary->net_unix : now_unix;
  const bool timeline_mode = ui.timeline_count > 0;
  const int64_t now_offset = now_unix - t0;
  const float now_deg = timeline_offset_deg(now_offset, timeline_mode);

  if (primary) {
    if (now_unix < t0) {
      pm_face_draw_annular_wedge(kCx, kCy, r_arc_inner, r_arc_outer, now_deg, 0.f,
                                 blend565(c_tminus, pm_gfx->color565(255, 255, 255), 0.25f));
    } else if (now_unix > t0) {
      pm_face_draw_annular_wedge(kCx, kCy, r_arc_inner, r_arc_outer, 0.f, now_deg,
                                 blend565(c_tplus, pm_gfx->color565(255, 255, 255), 0.25f));
    }
  }

  pm_gfx->drawCircle(kCx, kCy, r_outer, c_ring_hi);
  pm_gfx->drawCircle(kCx, kCy, r_inner, c_ring);

  const uint16_t c_tick = pm_gfx->color565(55, 70, 100);
  const uint16_t c_label = pm_gfx->color565(130, 145, 175);
  if (timeline_mode) {
    struct {
      int32_t offset;
      const char *label;
    } marks[] = {{-3600, "T-60"}, {-1800, "T-30"}, {1800, "T+30"}, {3600, "T+60"}};
    for (const auto &m : marks) {
      const float deg = timeline_offset_deg(m.offset, true);
      draw_ring_tick(kCx, kCy, r_inner, r_outer, deg, c_tick);
      pm_face_draw_label_at_polar(kCx, kCy, r_outer + 6, pm_face_deg_to_rad(deg), m.label, c_label);
    }
  } else {
    for (int h = -12; h <= 12; h += 3) {
      if (h == 0) {
        continue;
      }
      const float deg = static_cast<float>(h) * 15.f;
      draw_ring_tick(kCx, kCy, r_inner, r_outer, deg, c_tick);
      char lbl[8];
      snprintf(lbl, sizeof(lbl), h < 0 ? "T%d" : "T+%d", h);
      pm_face_draw_label_at_polar(kCx, kCy, r_outer + 6, pm_face_deg_to_rad(deg), lbl, c_label);
    }
  }

  draw_ring_tick(kCx, kCy, r_inner - 2, r_outer + 2, 0.f, c_launch);
  pm_face_draw_label_at_polar(kCx, kCy, r_outer + 8, pm_face_deg_to_rad(0.f), "T-0", c_launch);
  draw_timeline_events(ui);

  const uint16_t c_inner = blend565(c_bg, pm_gfx->color565(30, 45, 80), 0.55f);
  pm_gfx->fillCircle(kCx, kCy, r_disk, c_inner);
  pm_gfx->drawCircle(kCx, kCy, r_disk, pm_gfx->color565(90, 120, 170));

  if (primary) {
    draw_now_bead_at(kCx, kCy, r_markers, now_deg, pm_gfx->color565(255, 250, 230));

    for (int i = 0; i < ui.count; ++i) {
      if (i == selected_idx) {
        continue;
      }
      const PmRocketLaunch &lv = ui.launches[i];
      if (!lv.valid || !within_launch_ring(lv.net_unix, t0)) {
        continue;
      }
      const float deg = t_rel_deg(lv.net_unix, t0);
      draw_now_bead_at(kCx, kCy, r_markers, deg, pm_gfx->color565(120, 180, 255));
    }
  }
}

bool encode_stream_qr(const char *url) {
  if (!url || !url[0]) {
    return false;
  }
  if (s_stream_qr_modules_valid && strcmp(url, s_stream_qr_cached_url) == 0 && s_stream_qr_cached_size > 0) {
    return true;
  }
  if (!qrcodegen_encodeText(url, s_stream_qr_temp, s_stream_qr_out, qrcodegen_Ecc_LOW, qrcodegen_VERSION_MIN,
                            kStreamQrMaxVersion, qrcodegen_Mask_AUTO, true)) {
    s_stream_qr_modules_valid = false;
    s_stream_qr_cached_size = 0;
    return false;
  }
  strncpy(s_stream_qr_cached_url, url, sizeof(s_stream_qr_cached_url) - 1);
  s_stream_qr_cached_url[sizeof(s_stream_qr_cached_url) - 1] = '\0';
  s_stream_qr_modules_valid = true;
  s_stream_qr_cached_size = qrcodegen_getSize(s_stream_qr_out);
  return s_stream_qr_cached_size > 0;
}

bool draw_stream_qr(int cx, int cy, int max_px) {
  const int size = s_stream_qr_cached_size;
  if (size <= 0) {
    return false;
  }
  int mod = max_px / size;
  if (mod < 2) {
    mod = 2;
  }
  if (mod > 4) {
    mod = 4;
  }
  const int quiet = 4;
  const int total = mod * (size + quiet * 2);
  const int x0 = cx - total / 2;
  const int y0 = cy - total / 2;
  const uint16_t fg = pm_gfx->color565(8, 8, 12);
  const uint16_t bg = pm_gfx->color565(248, 248, 252);
  pm_gfx->fillRect(x0, y0, total, total, bg);
  for (int y = 0; y < size; ++y) {
    int x = 0;
    while (x < size) {
      const bool on = qrcodegen_getModule(s_stream_qr_out, x, y);
      int run = 1;
      while (x + run < size && qrcodegen_getModule(s_stream_qr_out, x + run, y) == on) {
        ++run;
      }
      if (on) {
        pm_gfx->fillRect(x0 + (x + quiet) * mod, y0 + (y + quiet) * mod, run * mod, mod, fg);
      }
      x += run;
    }
  }
  return true;
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
  const uint16_t c_live = pm_gfx->color565(255, 90, 80);

  pm_face_draw_centered_line(time_line, kCy - 8, c_big, 2, 2);
  if (next && next->webcast_live) {
    pm_face_draw_centered_line("● LIVE", kCy + 14, c_live, 1, 1);
  } else {
    pm_face_draw_centered_line("LAUNCH CLOCK", kCy + 18, c_accent, 1, 1);
  }

  if (next) {
    char line[48];
    pm_face_rocket_format_t_rel(next->net_unix, line, sizeof(line));
    const int panel_w = 178;
    const int panel_h = 44;
    const int panel_x = kCx - panel_w / 2;
    const int panel_y = kCy + 27;
    pm_gfx->fillRoundRect(panel_x, panel_y, panel_w, panel_h, 5, pm_gfx->color565(248, 248, 240));
    pm_gfx->drawRoundRect(panel_x, panel_y, panel_w, panel_h, 5, pm_gfx->color565(255, 220, 130));
    pm_face_draw_centered_line(line, panel_y + 10, pm_gfx->color565(8, 12, 18), 3, 3);
    if (next->status_abbrev[0]) {
      snprintf(line, sizeof(line), "%s", next->status_abbrev);
      pm_face_draw_centered_line(line, kCy + 76, c_dim, 1, 1);
    }
  }
}

void draw_minute_countdown_ring(const PmRocketLaunch *next) {
  if (!next || !pm_time_valid()) {
    return;
  }
  int64_t delta = static_cast<int64_t>(time(nullptr)) - next->net_unix;
  const bool after = delta >= 0;
  if (!after) {
    delta = -delta;
  }

  float sec = static_cast<float>(delta % 60);
  const float frac = static_cast<float>(millis() % 1000u) / 1000.f;
  if (after) {
    sec += frac;
    if (sec >= 60.f) {
      sec -= 60.f;
    }
  } else {
    sec -= frac;
    if (sec < 0.f) {
      sec += 60.f;
    }
  }

  const int r = 112;
  const uint16_t base = pm_gfx->color565(42, 58, 92);
  const uint16_t hot = after ? pm_gfx->color565(255, 150, 86) : pm_gfx->color565(255, 232, 150);
  const uint16_t bead = pm_gfx->color565(255, 255, 245);
  pm_gfx->drawCircle(kCx, kCy, r, base);
  pm_gfx->drawCircle(kCx, kCy, r + 1, base);
  const int lit = static_cast<int>(ceilf(sec));
  for (int i = 0; i < 60; ++i) {
    if ((!after && i >= lit) || (after && i > lit)) {
      continue;
    }
    const float deg = static_cast<float>(i) * 6.f - 90.f;
    const float a = pm_face_deg_to_rad(deg);
    const int x = kCx + static_cast<int>(lrintf(cosf(a) * static_cast<float>(r)));
    const int y = kCy + static_cast<int>(lrintf(sinf(a) * static_cast<float>(r)));
    pm_gfx->drawPixel(x, y, hot);
    pm_gfx->drawPixel(x + 1, y, hot);
  }

  const float bead_deg = sec * 6.f - 90.f;
  const float a = pm_face_deg_to_rad(bead_deg);
  const int bx = kCx + static_cast<int>(lrintf(cosf(a) * static_cast<float>(r)));
  const int by = kCy + static_cast<int>(lrintf(sinf(a) * static_cast<float>(r)));
  pm_gfx->fillCircle(bx, by, 3, bead);
}

void draw_stream_overlay(const PmRocketLaunch *next) {
  const uint16_t bg = pm_gfx->color565(6, 10, 24);
  pm_gfx->fillScreen(bg);
  if (pm_rocket_pad_image_ready()) {
    pm_rocket_pad_image_draw_background(kCx, kCy + 12, min(LCD_WIDTH, LCD_HEIGHT) / 2, bg, 0.72f);
  }
  const uint16_t c_hi = pm_gfx->color565(220, 230, 245);
  const uint16_t c_dim = pm_gfx->color565(130, 145, 170);
  const uint16_t c_accent = pm_gfx->color565(255, 214, 132);
  pm_face_draw_centered_line("LIVE A/V STREAM", 36, c_hi, 2, 2);
  if (next && next->webcast_live) {
    pm_face_draw_centered_line("● LIVE NOW", 64, pm_gfx->color565(255, 100, 90), 1, 1);
  } else {
    pm_face_draw_centered_line("standby feed", 64, c_dim, 1, 1);
  }

  const int frame_w = 286;
  const int frame_h = 180;
  const int frame_x = kCx - frame_w / 2;
  const int frame_y = 114;
  pm_gfx->fillRoundRect(frame_x, frame_y, frame_w, frame_h, 8, pm_gfx->color565(10, 16, 28));
  pm_gfx->drawRoundRect(frame_x, frame_y, frame_w, frame_h, 8, c_accent);
  pm_gfx->drawRect(frame_x + 8, frame_y + 8, frame_w - 16, frame_h - 16, pm_gfx->color565(70, 88, 118));

  const uint32_t t = millis();
  const int cx = kCx;
  const int cy = frame_y + frame_h / 2;
  for (int i = 0; i < 7; ++i) {
    const float phase = static_cast<float>((t / 80u + i * 7u) % 40u) / 40.f;
    const int h = 28 + static_cast<int>(sinf((phase + i * 0.17f) * pm_face_k_two_pi) * 20.f);
    const int x = cx - 54 + i * 18;
    pm_gfx->fillRoundRect(x, cy - h / 2, 9, h, 3, i == 3 ? c_accent : pm_gfx->color565(105, 170, 240));
  }
  pm_face_draw_centered_line("audio/video feed", frame_y + frame_h + 24, c_hi, 1, 1);
  pm_face_draw_centered_line(next && next->provider[0] ? next->provider : "SpaceX", frame_y + frame_h + 44, c_dim, 1, 1);

  if (next) {
    char countdown[32];
    pm_face_rocket_format_t_rel(next->net_unix, countdown, sizeof(countdown));
    pm_gfx->fillRoundRect(kCx - 88, 320, 176, 38, 5, pm_gfx->color565(248, 248, 240));
    pm_gfx->drawRoundRect(kCx - 88, 320, 176, 38, 5, c_accent);
    pm_face_draw_centered_line(countdown, 329, pm_gfx->color565(8, 12, 18), 2, 2);
  }
  pm_face_draw_centered_line("tap to return", 384, c_dim, 1, 1);
}

void draw_upcoming_list(const PmRocketStatus &ui) {
  const uint16_t c_time = pm_gfx->color565(180, 195, 220);
  const uint16_t c_name = pm_gfx->color565(220, 230, 245);
  const uint16_t c_label = pm_gfx->color565(110, 125, 150);
  int y = 318;
  pm_face_draw_centered_line("UPCOMING", y, c_label, 1, 1);
  y += 20;

  const int rows = ui.count < 3 ? ui.count : 3;
  int start = selected_launch_index(ui) - 1;
  if (start < 0) {
    start = 0;
  }
  if (start + rows > ui.count) {
    start = ui.count - rows;
  }
  if (start < 0) {
    start = 0;
  }
  const int selected_idx = selected_launch_index(ui);
  for (int row = 0; row < rows; ++row) {
    const int i = start + row;
    const PmRocketLaunch &lv = ui.launches[i];
    char when[16];
    char mission[28];
    format_launch_local(lv.net_unix, when, sizeof(when));
    short_mission_label(lv, mission, sizeof(mission));
    char line[44];
    snprintf(line, sizeof(line), "%c %s  %s", i == selected_idx ? '>' : ' ', when, mission);
    pm_face_draw_centered_line(line, y, i == selected_idx ? c_name : c_time, 1, 1);
    y += 18;
  }
}

}  // namespace

void pm_face_rocket_format_t_rel(int64_t launch_unix, char *out, size_t cap) {
  if (!out || cap == 0) {
    return;
  }
  const int64_t now = static_cast<int64_t>(time(nullptr));
  int64_t delta = now - launch_unix;
  const bool after = delta >= 0;
  if (!after) {
    delta = -delta;
  }
  const int64_t days = delta / 86400;
  delta %= 86400;
  const int h = static_cast<int>(delta / 3600);
  const int m = static_cast<int>((delta % 3600) / 60);
  const int s = static_cast<int>(delta % 60);
  if (days > 0) {
    snprintf(out, cap, after ? "T+%lldd %02d:%02d:%02d" : "T-%lldd %02d:%02d:%02d",
             static_cast<long long>(days), h, m, s);
  } else if (h > 0) {
    snprintf(out, cap, after ? "T+%02d:%02d:%02d" : "T-%02d:%02d:%02d", h, m, s);
  } else {
    snprintf(out, cap, after ? "T+%02d:%02d" : "T-%02d:%02d", m, s);
  }
}

void pm_face_rocket_format_countdown(int64_t net_unix, char *out, size_t cap) {
  pm_face_rocket_format_t_rel(net_unix, out, cap);
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

bool pm_face_rocket_has_stream(void) {
  const PmRocketLaunch *next = selected_launch(g_rocket_ui);
  return next && next->webcast_url[0] != '\0';
}

bool pm_face_rocket_stream_qr_visible(void) { return s_stream_qr_visible; }

void pm_face_rocket_set_stream_qr_visible(bool visible) { s_stream_qr_visible = visible; }

void pm_face_rocket_toggle_stream_qr(void) {
  if (!pm_face_rocket_has_stream()) {
    s_stream_qr_visible = false;
    return;
  }
  s_stream_qr_visible = !s_stream_qr_visible;
  s_stream_last_repaint_ms = 0;
}

bool pm_face_rocket_stream_needs_repaint(uint32_t now_ms) {
  if (!s_stream_qr_visible) {
    s_stream_last_repaint_ms = 0;
    return false;
  }
  if (s_stream_last_repaint_ms == 0 || now_ms - s_stream_last_repaint_ms >= 100u) {
    s_stream_last_repaint_ms = now_ms;
    return true;
  }
  return false;
}

bool pm_face_rocket_needs_repaint(uint32_t now_ms) {
  if (!pm_time_valid()) {
    s_clock_last_repaint_ms = 0;
    return false;
  }
  if (s_clock_last_repaint_ms == 0 || now_ms - s_clock_last_repaint_ms >= 250u) {
    s_clock_last_repaint_ms = now_ms;
    return true;
  }
  return false;
}

bool pm_face_rocket_tap(int16_t x, int16_t y, char *banner, size_t banner_cap) {
  if (s_stream_qr_visible) {
    pm_face_rocket_toggle_stream_qr();
    if (banner && banner_cap > 0) {
      snprintf(banner, banner_cap, "launch: clock");
    }
    return true;
  }
  if (g_rocket_ui.timeline_count > 0) {
    const float dx = static_cast<float>(x - kCx);
    const float dy = static_cast<float>(y - kCy);
    const float r = sqrtf(dx * dx + dy * dy);
    const int R = min(LCD_WIDTH, LCD_HEIGHT) / 2;
    if (r >= static_cast<float>(R - 64) && r <= static_cast<float>(R + 8)) {
      const float tap_deg = normalize_deg180((atan2f(dy, dx) * 180.f / pm_face_k_pi) + 90.f);
      int best_idx = -1;
      float best_dist = 999.f;
      for (int i = 0; i < g_rocket_ui.timeline_count; ++i) {
        const PmRocketTimelineEvent &e = g_rocket_ui.timeline[i];
        if (!e.valid) {
          continue;
        }
        const float ev_deg = normalize_deg180(timeline_offset_deg(e.offset_sec, true));
        const float dist = fabsf(normalize_deg180(tap_deg - ev_deg));
        if (dist < best_dist) {
          best_dist = dist;
          best_idx = i;
        }
      }
      if (best_idx >= 0 && best_dist <= 14.f) {
        s_timeline_selected_idx = best_idx;
        s_timeline_selected_until_ms = millis() + 10000u;
        s_stream_qr_visible = false;
        if (banner && banner_cap > 0) {
          char t[16];
          format_timeline_offset(g_rocket_ui.timeline[best_idx].offset_sec, t, sizeof(t));
          snprintf(banner, banner_cap, "%s %.24s", t, g_rocket_ui.timeline[best_idx].label);
        }
        return true;
      }
    }
  }
  if (banner && banner_cap > 0) {
    snprintf(banner, banner_cap, "launch: timeline");
  }
  return false;
}

bool pm_face_rocket_cycle_launch(int delta) {
  if (!g_rocket_ui.ok || g_rocket_ui.count <= 0) {
    return false;
  }
  const int old = selected_launch_index(g_rocket_ui);
  int next = old + delta;
  if (next < 0) {
    next = g_rocket_ui.count - 1;
  } else if (next >= g_rocket_ui.count) {
    next = 0;
  }
  s_rocket_selected_idx = next;
  s_stream_qr_visible = false;
  s_timeline_selected_idx = -1;
  return next != old;
}

int pm_face_rocket_selected_index(void) { return selected_launch_index(g_rocket_ui); }

void pm_face_rocket_reset_selection(void) {
  s_rocket_selected_idx = 0;
  s_stream_qr_visible = false;
  s_timeline_selected_idx = -1;
}

void pm_face_rocket_draw() {
  const uint16_t c_dim = pm_gfx->color565(130, 140, 165);
  const uint16_t c_accent = pm_gfx->color565(120, 200, 255);

  const PmRocketLaunch *next = selected_launch(g_rocket_ui);
  s_stream_qr_visible = false;
  if (s_stream_qr_visible && next && next->webcast_url[0]) {
    draw_stream_overlay(next);
    return;
  }

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
    pm_face_rocket_reset_selection();
    draw_launch_clock_dial(now_unix, g_rocket_ui);
    draw_center_clock(nullptr);
    pm_face_draw_centered_line("loading…", 292, c_dim, 1, 1);
    return;
  }

  if (!g_rocket_ui.ok || g_rocket_ui.count <= 0) {
    pm_face_rocket_reset_selection();
    draw_launch_clock_dial(now_unix, g_rocket_ui);
    draw_center_clock(nullptr);
    pm_face_draw_centered_line(g_rocket_ui.error[0] ? g_rocket_ui.error : "unavailable", 292,
                               pm_gfx->color565(255, 120, 110), 1, 1);
    return;
  }

  draw_launch_clock_dial(now_unix, g_rocket_ui);
  draw_minute_countdown_ring(selected_launch(g_rocket_ui));
  draw_selected_timeline_label(g_rocket_ui);
  draw_center_clock(selected_launch(g_rocket_ui));
  draw_upcoming_list(g_rocket_ui);
}
