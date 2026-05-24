#include "faces/live_transits/pm_face_live_transits.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include "faces/astrology/pm_face_astrology.h"
#include "faces/astrology/pm_zodiac_glyphs.h"
#include "faces/shared/pm_face_draw.h"
#include "pm_display.h"
#include "pm_transit.h"
#include "pm_wifi_ntp.h"

namespace {

constexpr int kCx = pm_face_lcd_cx;
constexpr int kCy = pm_face_lcd_cy;
constexpr int kTrailDays = 4;

double norm360(double v) {
  v = fmod(v, 360.0);
  if (v < 0.0) {
    v += 360.0;
  }
  return v;
}

int sign_index(double lon) {
  return static_cast<int>(norm360(lon) / 30.0) % 12;
}

float lon_to_ang(double lon) {
  return static_cast<float>(pm_face_k_pi + norm360(lon) * (pm_face_k_pi / 180.0f));
}

double signed_lon_delta(double from_lon, double to_lon) {
  double d = norm360(to_lon - from_lon);
  if (d > 180.0) {
    d -= 360.0;
  }
  return d;
}

time_t utc_tm_to_epoch(const struct tm *utc) {
  if (!utc) {
    return static_cast<time_t>(-1);
  }
  struct tm t = *utc;
  t.tm_isdst = 0;
  const char *prev = getenv("TZ");
  char saved[48] = {};
  if (prev) {
    strncpy(saved, prev, sizeof(saved) - 1);
  }
  setenv("TZ", "UTC0", 1);
  tzset();
  const time_t e = mktime(&t);
  if (prev) {
    setenv("TZ", saved, 1);
  } else {
    unsetenv("TZ");
  }
  tzset();
  return e;
}

bool compute_at_epoch(time_t epoch, PmTransitPositions *out) {
  if (!out || epoch < 0) {
    return false;
  }
  struct tm utc = {};
  gmtime_r(&epoch, &utc);
  pm_transit_compute_utc(&utc, out);
  return out->ok;
}

bool find_next_moon_ingress(time_t now_epoch, const PmTransitPositions *now_tp, time_t *event_epoch,
                            PmTransitPositions *event_tp) {
  if (!now_tp || !now_tp->ok || !event_epoch || !event_tp) {
    return false;
  }
  const int start_sign = sign_index(now_tp->lon[kPmBodyMoon]);
  time_t lo = now_epoch;
  PmTransitPositions hi_tp = {};
  time_t hi = 0;
  bool bracketed = false;
  for (int minutes = 30; minutes <= 72 * 60; minutes += 60) {
    hi = now_epoch + static_cast<time_t>(minutes) * 60;
    if (!compute_at_epoch(hi, &hi_tp)) {
      return false;
    }
    if (sign_index(hi_tp.lon[kPmBodyMoon]) != start_sign) {
      bracketed = true;
      break;
    }
    lo = hi;
  }
  if (!bracketed) {
    return false;
  }
  PmTransitPositions mid_tp = {};
  for (int i = 0; i < 8; ++i) {
    const time_t mid = lo + (hi - lo) / 2;
    if (!compute_at_epoch(mid, &mid_tp)) {
      return false;
    }
    if (sign_index(mid_tp.lon[kPmBodyMoon]) == start_sign) {
      lo = mid;
    } else {
      hi = mid;
      hi_tp = mid_tp;
    }
  }
  *event_epoch = hi;
  *event_tp = hi_tp;
  return true;
}

uint16_t body_color(PmEphemBody body, bool next) {
  if (next) {
    return pm_gfx->color565(160, 225, 255);
  }
  switch (body) {
    case kPmBodySun:
      return pm_gfx->color565(255, 210, 90);
    case kPmBodyMoon:
      return pm_gfx->color565(215, 225, 238);
    case kPmBodyMercury:
      return pm_gfx->color565(174, 184, 198);
    case kPmBodyVenus:
      return pm_gfx->color565(255, 188, 142);
    case kPmBodyMars:
      return pm_gfx->color565(235, 92, 70);
    case kPmBodyJupiter:
      return pm_gfx->color565(220, 178, 122);
    case kPmBodySaturn:
      return pm_gfx->color565(190, 172, 142);
    default:
      return pm_gfx->color565(200, 205, 215);
  }
}

void draw_starfield(uint32_t seed) {
  const uint16_t c = pm_gfx->color565(54, 62, 82);
  for (int i = 0; i < 56; ++i) {
    seed = seed * 1664525u + 1013904223u;
    const int x = static_cast<int>((seed >> 8) % LCD_WIDTH);
    seed = seed * 1664525u + 1013904223u;
    const int y = static_cast<int>((seed >> 8) % LCD_HEIGHT);
    const int dx = x - kCx;
    const int dy = y - kCy;
    const int star_r = pm_face_scale_i(214);
    if (dx * dx + dy * dy < star_r * star_r) {
      pm_gfx->drawPixel(x, y, c);
    }
  }
}

void draw_sphere_frame(int cx, int cy, int r, uint16_t rim, uint16_t grid) {
  pm_gfx->drawCircle(cx, cy, r, rim);
  pm_gfx->drawCircle(cx, cy, r - 1, pm_gfx->color565(28, 36, 54));
  pm_gfx->drawEllipse(cx, cy, r, r / 3, grid);
  pm_gfx->drawEllipse(cx, cy, r * 2 / 3, r, grid);
  pm_gfx->drawLine(cx - r, cy, cx + r, cy, pm_gfx->color565(34, 42, 62));
  pm_gfx->drawLine(cx, cy - r, cx, cy + r, pm_gfx->color565(34, 42, 62));
}

void draw_bodies_on_sphere(const PmTransitPositions *tp, int cx, int cy, int r, int highlight_body,
                           bool next_style) {
  const int body_r = r - pm_face_scale_i(15);
  for (int i = 0; i < kPmBodyCount; ++i) {
    const float a = lon_to_ang(tp->lon[i]);
    const int bx = cx + static_cast<int>(lrintf(cosf(a) * static_cast<float>(body_r)));
    const int by = cy + static_cast<int>(lrintf(sinf(a) * static_cast<float>(body_r) * 0.74f));
    const bool hi = i == highlight_body;
    if (hi) {
      pm_gfx->fillCircle(bx, by, pm_face_scale_i(next_style ? 13 : 12), pm_gfx->color565(24, 54, 72));
      pm_gfx->drawCircle(bx, by, pm_face_scale_i(next_style ? 15 : 14), pm_gfx->color565(150, 230, 255));
    }
    pm_planet_draw_at_polar(pm_gfx, cx, cy, body_r, a, i, body_color(static_cast<PmEphemBody>(i), next_style),
                            hi);
  }
}

bool body_is_retrograde(int body_idx, const PmTransitPositions *now_tp, const PmTransitPositions *future_tp) {
  if (!now_tp || !future_tp || !now_tp->ok || !future_tp->ok) {
    return false;
  }
  if (body_idx == kPmBodySun || body_idx == kPmBodyMoon) {
    return false;
  }
  return signed_lon_delta(now_tp->lon[body_idx], future_tp->lon[body_idx]) < -0.02;
}

int retrograde_count(const PmTransitPositions *now_tp, const PmTransitPositions *future_tp) {
  int count = 0;
  for (int i = 0; i < kPmBodyCount; ++i) {
    if (body_is_retrograde(i, now_tp, future_tp)) {
      ++count;
    }
  }
  return count;
}

void draw_motion_tick(float a, int r, bool retrograde) {
  const float tx = -sinf(a);
  const float ty = cosf(a);
  const float dir = retrograde ? -1.f : 1.f;
  const int cx = kCx + static_cast<int>(lrintf(cosf(a) * static_cast<float>(r)));
  const int cy = kCy + static_cast<int>(lrintf(sinf(a) * static_cast<float>(r)));
  const int x0 = cx - static_cast<int>(lrintf(tx * dir * 4.f));
  const int y0 = cy - static_cast<int>(lrintf(ty * dir * 4.f));
  const int x1 = cx + static_cast<int>(lrintf(tx * dir * 9.f));
  const int y1 = cy + static_cast<int>(lrintf(ty * dir * 9.f));
  const uint16_t col = retrograde ? pm_gfx->color565(255, 112, 138) : pm_gfx->color565(82, 146, 176);
  pm_gfx->drawLine(x0, y0, x1, y1, col);
  pm_gfx->fillCircle(x1, y1, retrograde ? 3 : 2, col);
}

uint16_t trail_color(bool retrograde, int age) {
  if (retrograde) {
    switch (age) {
      case 1:
        return pm_gfx->color565(168, 58, 82);
      case 2:
        return pm_gfx->color565(118, 42, 62);
      case 3:
        return pm_gfx->color565(82, 32, 48);
      default:
        return pm_gfx->color565(56, 24, 36);
    }
  }
  switch (age) {
    case 1:
      return pm_gfx->color565(62, 108, 138);
    case 2:
      return pm_gfx->color565(46, 78, 106);
    case 3:
      return pm_gfx->color565(34, 56, 82);
    default:
      return pm_gfx->color565(26, 40, 62);
  }
}

void draw_recent_motion_trails(const PmTransitPositions *now_tp, const PmTransitPositions *motion_tp,
                               const PmTransitPositions trail_tp[], int trail_count) {
  if (!now_tp || !now_tp->ok || !trail_tp || trail_count <= 0) {
    return;
  }
  const int r_base = pm_face_scale_i(139);
  for (int body = 0; body < kPmBodyCount; ++body) {
    const bool rx = body_is_retrograde(body, now_tp, motion_tp);
    int prev_x = 0;
    int prev_y = 0;
    bool have_prev = false;
    for (int age = trail_count; age >= 1; --age) {
      const PmTransitPositions &sample = trail_tp[age - 1];
      if (!sample.ok) {
        continue;
      }
      const float a = lon_to_ang(sample.lon[body]);
      const int r = r_base + (body % 3) * pm_face_scale_i(6);
      const int x = kCx + static_cast<int>(lrintf(cosf(a) * static_cast<float>(r)));
      const int y = kCy + static_cast<int>(lrintf(sinf(a) * static_cast<float>(r)));
      const uint16_t col = trail_color(rx, age);
      if (have_prev) {
        pm_gfx->drawLine(prev_x, prev_y, x, y, col);
      }
      pm_gfx->fillCircle(x, y, pm_face_scale_i(age == 1 ? 3 : 2), col);
      prev_x = x;
      prev_y = y;
      have_prev = true;
    }
    const float now_a = lon_to_ang(now_tp->lon[body]);
    const int r = r_base + (body % 3) * pm_face_scale_i(6);
    const int now_x = kCx + static_cast<int>(lrintf(cosf(now_a) * static_cast<float>(r)));
    const int now_y = kCy + static_cast<int>(lrintf(sinf(now_a) * static_cast<float>(r)));
    const uint16_t col = rx ? pm_gfx->color565(210, 78, 104) : pm_gfx->color565(74, 132, 164);
    if (have_prev) {
      pm_gfx->drawLine(prev_x, prev_y, now_x, now_y, col);
    }
    pm_gfx->fillCircle(now_x, now_y, pm_face_scale_i(3), col);
  }
}

void draw_radial_transit_ring(const PmTransitPositions *tp, const PmTransitPositions *event_tp,
                              const PmTransitPositions *motion_tp, const PmTransitPositions trail_tp[],
                              int trail_count, time_t now_epoch, time_t event_epoch) {
  const int r_outer = pm_face_scale_i(214);
  const int r_inner = pm_face_scale_i(192);
  const int r_body = pm_face_scale_i(174);
  const int r_motion = pm_face_scale_i(158);
  const uint16_t c_track = pm_gfx->color565(18, 28, 46);
  const uint16_t c_tick = pm_gfx->color565(54, 68, 94);
  const uint16_t c_arc = pm_gfx->color565(78, 210, 242);
  const uint16_t c_elapsed = pm_gfx->color565(96, 118, 160);

  pm_face_draw_annular_wedge(kCx, kCy, r_inner, r_outer, 0.f, 360.f, c_track);
  for (int s = 0; s < 12; ++s) {
    const float a = pm_face_deg_to_rad(static_cast<float>(s) * 30.f);
    const int x0 = kCx + static_cast<int>(lrintf(cosf(a) * static_cast<float>(r_inner - pm_face_scale_i(2))));
    const int y0 = kCy + static_cast<int>(lrintf(sinf(a) * static_cast<float>(r_inner - pm_face_scale_i(2))));
    const int x1 = kCx + static_cast<int>(lrintf(cosf(a) * static_cast<float>(r_outer + pm_face_scale_i(1))));
    const int y1 = kCy + static_cast<int>(lrintf(sinf(a) * static_cast<float>(r_outer + pm_face_scale_i(1))));
    pm_gfx->drawLine(x0, y0, x1, y1, c_tick);
  }

  const double moon_lon = norm360(tp->lon[kPmBodyMoon]);
  const int current_sign = sign_index(moon_lon);
  const float sign_progress = static_cast<float>((moon_lon - static_cast<double>(current_sign) * 30.0) / 30.0);
  const float progress_deg = sign_progress * 360.f;
  pm_face_draw_annular_wedge(kCx, kCy, r_inner + pm_face_scale_i(3), r_outer - pm_face_scale_i(3), 0.f,
                             progress_deg, c_elapsed);
  if (event_tp && event_tp->ok && event_epoch > now_epoch) {
    pm_face_draw_annular_wedge(kCx, kCy, r_inner + pm_face_scale_i(7), r_outer - pm_face_scale_i(7), progress_deg,
                               360.f, c_arc);
  }

  draw_recent_motion_trails(tp, motion_tp, trail_tp, trail_count);

  for (int i = 0; i < kPmBodyCount; ++i) {
    const float a = lon_to_ang(tp->lon[i]);
    const int bx = kCx + static_cast<int>(lrintf(cosf(a) * static_cast<float>(r_body)));
    const int by = kCy + static_cast<int>(lrintf(sinf(a) * static_cast<float>(r_body)));
    const bool hi = i == kPmBodyMoon;
    const bool rx = body_is_retrograde(i, tp, motion_tp);
    draw_motion_tick(a, r_motion, rx);
    pm_gfx->fillCircle(bx, by, pm_face_scale_i(hi ? 7 : 4), body_color(static_cast<PmEphemBody>(i), false));
    if (hi) {
      pm_gfx->drawCircle(bx, by, pm_face_scale_i(10), c_arc);
    } else if (rx) {
      pm_gfx->drawCircle(bx, by, pm_face_scale_i(7), pm_gfx->color565(255, 112, 138));
    }
  }
}

void format_until(time_t now_epoch, time_t event_epoch, char *out, size_t cap) {
  int64_t mins = static_cast<int64_t>((event_epoch - now_epoch + 30) / 60);
  if (mins < 0) {
    mins = 0;
  }
  if (mins < 90) {
    snprintf(out, cap, "%dm", static_cast<int>(mins));
  } else if (mins < 36 * 60) {
    snprintf(out, cap, "%dh %02dm", static_cast<int>(mins / 60), static_cast<int>(mins % 60));
  } else {
    snprintf(out, cap, "%dd %02dh", static_cast<int>(mins / 1440), static_cast<int>((mins % 1440) / 60));
  }
}

void draw_footer(const PmTransitPositions *now_tp, time_t now_epoch, time_t event_epoch,
                 const PmTransitPositions *event_tp) {
  char now_line[42];
  char next_line[42];
  char until[16];
  format_until(now_epoch, event_epoch, until, sizeof(until));
  snprintf(now_line, sizeof(now_line), "NOW  Mo %s", pm_face_zodiac_abbr(now_tp->lon[kPmBodyMoon]));
  snprintf(next_line, sizeof(next_line), "NEXT Mo %s in %s", pm_face_zodiac_abbr(event_tp->lon[kPmBodyMoon]),
           until);
  pm_face_draw_centered_line(now_line, pm_face_scale_y(362), pm_gfx->color565(224, 230, 245), 2, 2);
  pm_face_draw_centered_line(next_line, pm_face_scale_y(392), pm_gfx->color565(135, 220, 255), 1, 1);
}

void draw_countdown_core(const PmTransitPositions *now_tp, time_t now_epoch, time_t event_epoch,
                         const PmTransitPositions *event_tp, const PmTransitPositions *motion_tp) {
  char until[16];
  char next_line[42];
  char rx_line[20];
  format_until(now_epoch, event_epoch, until, sizeof(until));
  const int rx_count = retrograde_count(now_tp, motion_tp);
  snprintf(next_line, sizeof(next_line), "Mo -> %s", pm_face_zodiac_abbr(event_tp->lon[kPmBodyMoon]));
  snprintf(rx_line, sizeof(rx_line), "Rx %d", rx_count);
  pm_gfx->fillCircle(kCx, kCy, pm_face_scale_i(66), pm_gfx->color565(6, 11, 22));
  pm_gfx->drawCircle(kCx, kCy, pm_face_scale_i(66), pm_gfx->color565(38, 62, 86));
  pm_face_draw_centered_line(until, kCy - 17, pm_gfx->color565(230, 244, 255), 3, 3);
  pm_face_draw_centered_line("to next transit", kCy + 17, pm_gfx->color565(118, 138, 166), 1, 1);
  pm_face_draw_centered_line(next_line, kCy + 36, pm_gfx->color565(135, 220, 255), 1, 1);
  pm_face_draw_centered_line(rx_line, kCy + 53, rx_count > 0 ? pm_gfx->color565(255, 112, 138)
                                                             : pm_gfx->color565(78, 94, 118),
                             1, 1);
}

}  // namespace

void pm_face_live_transits_draw(const struct tm *tm_local, bool valid_local) {
  (void)tm_local;
  pm_gfx->fillScreen(pm_gfx->color565(5, 9, 18));
  draw_starfield(millis() / 900u + 17u);

  if (!valid_local) {
    pm_face_draw_centered_line("live transits", 188, pm_gfx->color565(170, 220, 255), 2, 2);
    pm_face_draw_centered_line("need UTC time", 226, pm_gfx->color565(120, 132, 154), 2, 2);
    return;
  }

  struct tm utc = {};
  pm_time_utc(&utc);
  const time_t now_epoch = utc_tm_to_epoch(&utc);
  PmTransitPositions now_tp = {};
  if (!compute_at_epoch(now_epoch, &now_tp)) {
    pm_face_draw_centered_line("live transits", 188, pm_gfx->color565(170, 220, 255), 2, 2);
    pm_face_draw_centered_line("ephemeris retry", 226, pm_gfx->color565(120, 132, 154), 2, 2);
    return;
  }

  PmTransitPositions next_tp = {};
  PmTransitPositions motion_tp = {};
  PmTransitPositions trail_tp[kTrailDays] = {};
  time_t next_epoch = 0;
  const bool have_next = find_next_moon_ingress(now_epoch, &now_tp, &next_epoch, &next_tp);
  const bool have_motion = compute_at_epoch(now_epoch + 24 * 60 * 60, &motion_tp);
  int trail_count = 0;
  for (int i = 0; i < kTrailDays; ++i) {
    const time_t sample_epoch = now_epoch - static_cast<time_t>(kTrailDays - i) * 24 * 60 * 60;
    if (compute_at_epoch(sample_epoch, &trail_tp[i])) {
      trail_count = i + 1;
    }
  }

  pm_face_draw_centered_line("LIVE TRANSITS", pm_face_scale_y(26), pm_gfx->color565(176, 224, 255), 2, 2);
  pm_face_draw_centered_line("4-day motion + countdown", pm_face_scale_y(50), pm_gfx->color565(86, 100, 126), 1,
                             1);

  draw_radial_transit_ring(&now_tp, have_next ? &next_tp : nullptr, have_motion ? &motion_tp : nullptr,
                           trail_count > 0 ? trail_tp : nullptr, trail_count, now_epoch, next_epoch);

  draw_sphere_frame(pm_face_scale_x(122), pm_face_scale_y(172), pm_face_scale_i(58), pm_gfx->color565(78, 100, 132),
                    pm_gfx->color565(32, 48, 72));
  draw_sphere_frame(pm_face_scale_x(344), pm_face_scale_y(172), pm_face_scale_i(58), pm_gfx->color565(54, 122, 150),
                    pm_gfx->color565(24, 68, 88));
  draw_bodies_on_sphere(&now_tp, pm_face_scale_x(122), pm_face_scale_y(172), pm_face_scale_i(58), kPmBodyMoon,
                        false);
  draw_bodies_on_sphere(have_next ? &next_tp : &now_tp, pm_face_scale_x(344), pm_face_scale_y(172),
                        pm_face_scale_i(58), kPmBodyMoon, true);

  if (have_next) {
    draw_countdown_core(&now_tp, now_epoch, next_epoch, &next_tp, have_motion ? &motion_tp : nullptr);
    draw_footer(&now_tp, now_epoch, next_epoch, &next_tp);
  } else {
    pm_face_draw_centered_line("NOW  Mo", pm_face_scale_y(362), pm_gfx->color565(224, 230, 245), 2, 2);
    pm_face_draw_centered_line("next transit pending", pm_face_scale_y(392), pm_gfx->color565(135, 220, 255), 1, 1);
  }
  pm_face_draw_circumference_rainbow_24h(true);
}
