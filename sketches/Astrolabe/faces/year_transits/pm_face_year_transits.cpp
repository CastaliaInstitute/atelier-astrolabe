#include "faces/year_transits/pm_face_year_transits.h"

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "faces/shared/pm_face_draw.h"
#include "pin_config.h"
#include "pm_birth_nvs.h"
#include "pm_display.h"
#include "pm_transit.h"
#include "pm_wifi_ntp.h"

namespace {

constexpr int kMaxYearArcs = 36;
constexpr int kSampleMonths = 12;

struct YearArc {
  float start_day;
  float end_day;
  uint8_t lane;
  PmEphemBody body;
  PmNatalTarget target;
  PmTransitAspectKind aspect;
  char title[kPmTransitAspectTitleLen];
  char duration[kPmTransitDurationLabelLen];
  double orb;
};

struct YearTransitCache {
  bool ready;
  bool has_birth;
  uint16_t year;
  uint16_t birth_year;
  uint8_t birth_month;
  uint8_t birth_day;
  uint8_t birth_hour;
  uint8_t birth_minute;
  float birth_lat;
  float birth_lon;
  int32_t birth_tz;
  size_t arc_count;
  YearArc arcs[kMaxYearArcs];
};

YearTransitCache s_cache = {};
int s_selected_arc = -1;

static bool leap_year(int y) {
  return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

static int days_in_year(int y) {
  return leap_year(y) ? 366 : 365;
}

static int day_of_year_zero_based(uint16_t y, uint8_t m, uint8_t d) {
  static const uint16_t k_before_month[] = {0,   31,  59,  90,  120, 151,
                                            181, 212, 243, 273, 304, 334};
  int v = k_before_month[(m > 0 ? m : 1) - 1] + static_cast<int>(d) - 1;
  if (m > 2 && leap_year(y)) {
    ++v;
  }
  return v;
}

static void month_day_from_day(uint16_t year, float day, uint8_t *month_out, uint8_t *day_out) {
  static const uint8_t k_days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  int d = static_cast<int>(floorf(day));
  if (d < 0) {
    d = 0;
  }
  const int yd = days_in_year(year);
  if (d >= yd) {
    d = yd - 1;
  }
  uint8_t month = 1;
  for (uint8_t i = 0; i < 12; ++i) {
    uint8_t dim = k_days[i];
    if (i == 1 && leap_year(year)) {
      dim = 29;
    }
    if (d < dim) {
      month = static_cast<uint8_t>(i + 1);
      break;
    }
    d -= dim;
  }
  if (month_out) {
    *month_out = month;
  }
  if (day_out) {
    *day_out = static_cast<uint8_t>(d + 1);
  }
}

static void make_utc_midmonth(uint16_t year, uint8_t month, struct tm *out) {
  memset(out, 0, sizeof(*out));
  out->tm_year = static_cast<int>(year) - 1900;
  out->tm_mon = static_cast<int>(month) - 1;
  out->tm_mday = 15;
  out->tm_hour = 12;
  out->tm_isdst = 0;
}

static bool same_birth_signature(const PmBirthSpec *b) {
  if (!b || !b->valid || !s_cache.has_birth) {
    return false;
  }
  return s_cache.birth_year == b->year && s_cache.birth_month == b->month &&
         s_cache.birth_day == b->day && s_cache.birth_hour == b->hour &&
         s_cache.birth_minute == b->minute && fabsf(s_cache.birth_lat - b->lat_deg) < 0.0005f &&
         fabsf(s_cache.birth_lon - b->lon_deg) < 0.0005f && s_cache.birth_tz == b->tz_offset_sec;
}

static uint8_t lane_for_aspect(const PmTransitAspect *a) {
  if (!a) {
    return 0;
  }
  switch (a->transit_body) {
    case kPmBodyMoon:
      return 0;
    case kPmBodySun:
      return 1;
    case kPmBodyMercury:
    case kPmBodyVenus:
      return 2;
    case kPmBodyMars:
      return 3;
    case kPmBodyJupiter:
      return 4;
    case kPmBodySaturn:
      return 5;
    default:
      return 2;
  }
}

static float center_day_for_month(uint16_t year, uint8_t month) {
  return static_cast<float>(day_of_year_zero_based(year, month, 15)) + 0.5f;
}

static bool add_or_merge_arc(uint16_t year, const PmTransitAspect *a, float center_day) {
  if (!a) {
    return false;
  }
  if (a->transit_body == kPmBodyMoon || a->active_days < 2.0) {
    return false;
  }
  const float half_days = std::max(2.0f, static_cast<float>(a->active_days) * 0.5f);
  const int year_days = days_in_year(year);
  YearArc next = {};
  next.start_day = std::max(0.0f, center_day - half_days);
  next.end_day = std::min(static_cast<float>(year_days), center_day + half_days);
  next.lane = lane_for_aspect(a);
  next.body = a->transit_body;
  next.target = a->natal_target;
  next.aspect = a->aspect;
  next.orb = fabs(a->orb_delta_deg);
  snprintf(next.title, sizeof(next.title), "%s", a->title);
  snprintf(next.duration, sizeof(next.duration), "%s", a->duration_label);

  for (size_t i = 0; i < s_cache.arc_count; ++i) {
    YearArc *existing = &s_cache.arcs[i];
    if (existing->body == next.body && existing->target == next.target && existing->aspect == next.aspect &&
        next.start_day <= existing->end_day + 8.0f && next.end_day >= existing->start_day - 8.0f) {
      existing->start_day = std::min(existing->start_day, next.start_day);
      existing->end_day = std::max(existing->end_day, next.end_day);
      if (next.orb < existing->orb) {
        existing->orb = next.orb;
        snprintf(existing->title, sizeof(existing->title), "%s", next.title);
        snprintf(existing->duration, sizeof(existing->duration), "%s", next.duration);
      }
      return true;
    }
  }

  if (s_cache.arc_count >= kMaxYearArcs) {
    size_t worst = 0;
    for (size_t i = 1; i < s_cache.arc_count; ++i) {
      if (s_cache.arcs[i].orb > s_cache.arcs[worst].orb) {
        worst = i;
      }
    }
    if (next.orb >= s_cache.arcs[worst].orb) {
      return false;
    }
    s_cache.arcs[worst] = next;
    return true;
  }

  s_cache.arcs[s_cache.arc_count++] = next;
  return true;
}

static bool rebuild_cache(uint16_t year, const PmBirthSpec *birth) {
  memset(&s_cache, 0, sizeof(s_cache));
  s_selected_arc = -1;
  s_cache.year = year;
  if (!birth || !birth->valid) {
    s_cache.ready = true;
    s_cache.has_birth = false;
    return false;
  }

  PmNatalChart natal = {};
  if (!pm_transit_build_natal_chart(birth, &natal)) {
    s_cache.ready = true;
    s_cache.has_birth = false;
    return false;
  }

  s_cache.has_birth = true;
  s_cache.birth_year = birth->year;
  s_cache.birth_month = birth->month;
  s_cache.birth_day = birth->day;
  s_cache.birth_hour = birth->hour;
  s_cache.birth_minute = birth->minute;
  s_cache.birth_lat = birth->lat_deg;
  s_cache.birth_lon = birth->lon_deg;
  s_cache.birth_tz = birth->tz_offset_sec;

  for (uint8_t month = 1; month <= kSampleMonths; ++month) {
    struct tm sample = {};
    make_utc_midmonth(year, month, &sample);
    PmTransitPositions transit = {};
    pm_transit_compute_utc(&sample, &transit);
    PmTransitSnapshot snap = {};
    if (!pm_transit_snapshot_from_positions(&natal, &transit, nullptr, 0, &snap)) {
      continue;
    }
    const float center_day = center_day_for_month(year, month);
    for (size_t i = 0; i < snap.aspect_count; ++i) {
      (void)add_or_merge_arc(year, &snap.aspects[i], center_day);
    }
  }

  std::sort(s_cache.arcs, s_cache.arcs + s_cache.arc_count, [](const YearArc &a, const YearArc &b) {
    if (a.lane != b.lane) {
      return a.lane < b.lane;
    }
    if (fabs(a.start_day - b.start_day) > 0.1f) {
      return a.start_day < b.start_day;
    }
    return a.orb < b.orb;
  });

  s_cache.ready = true;
  if (s_cache.arc_count > 0) {
    s_selected_arc = 0;
  }
  return true;
}

static void ensure_cache(uint16_t year, const PmBirthSpec *birth) {
  if (s_cache.ready && s_cache.year == year) {
    if ((!birth || !birth->valid) && !s_cache.has_birth) {
      return;
    }
    if (same_birth_signature(birth)) {
      return;
    }
  }
  (void)rebuild_cache(year, birth);
}

static float day_to_deg(float day, int year_days) {
  return day * (360.0f / static_cast<float>(year_days));
}

static void draw_arc_segment(int cx, int cy, int r, float start_deg, float end_deg, uint16_t col, int width) {
  if (end_deg <= start_deg) {
    return;
  }
  const float span = end_deg - start_deg;
  const int steps = std::max(4, std::min(80, static_cast<int>(lrintf(span * 0.45f))));
  int px = 0;
  int py = 0;
  for (int i = 0; i <= steps; ++i) {
    const float deg = start_deg + span * (static_cast<float>(i) / static_cast<float>(steps));
    const float a = pm_face_deg_to_rad(deg);
    const int x = cx + static_cast<int>(lrintf(cosf(a) * static_cast<float>(r)));
    const int y = cy + static_cast<int>(lrintf(sinf(a) * static_cast<float>(r)));
    if (i > 0) {
      for (int w = -width; w <= width; ++w) {
        pm_gfx->drawLine(px + w, py, x + w, y, col);
        if (width > 0) {
          pm_gfx->drawLine(px, py + w, x, y + w, col);
        }
      }
    }
    px = x;
    py = y;
  }
}

static uint16_t aspect_color(PmTransitAspectKind aspect) {
  switch (aspect) {
    case kPmTransitAspectConjunction:
      return pm_gfx->color565(255, 226, 142);
    case kPmTransitAspectSextile:
      return pm_gfx->color565(116, 212, 255);
    case kPmTransitAspectSquare:
      return pm_gfx->color565(255, 112, 108);
    case kPmTransitAspectTrine:
      return pm_gfx->color565(132, 232, 170);
    case kPmTransitAspectOpposition:
      return pm_gfx->color565(204, 142, 255);
    default:
      return pm_gfx->color565(180, 190, 210);
  }
}

static const char *month_label(int m) {
  static const char *const k[] = {"J", "F", "M", "A", "M", "J", "J", "A", "S", "O", "N", "D"};
  return k[m % 12];
}

static const char *body_label(PmEphemBody b) {
  switch (b) {
    case kPmBodyMoon:
      return "Moon";
    case kPmBodySun:
      return "Sun";
    case kPmBodyMercury:
      return "Mercury";
    case kPmBodyVenus:
      return "Venus";
    case kPmBodyMars:
      return "Mars";
    case kPmBodyJupiter:
      return "Jupiter";
    case kPmBodySaturn:
      return "Saturn";
    default:
      return "Transit";
  }
}

static const YearArc *selected_arc(void) {
  if (!s_cache.ready || s_cache.arc_count == 0 || s_selected_arc < 0 ||
      s_selected_arc >= static_cast<int>(s_cache.arc_count)) {
    return nullptr;
  }
  return &s_cache.arcs[s_selected_arc];
}

static void draw_legend(void) {
  int y = 312;
  pm_gfx->setTextSize(1, 1);
  pm_gfx->setTextColor(pm_gfx->color565(185, 194, 218));
  size_t shown = 0;
  for (size_t i = 0; i < s_cache.arc_count && shown < 3; ++i) {
    const YearArc &a = s_cache.arcs[i];
    char line[64];
    snprintf(line, sizeof(line), "%s %s", body_label(a.body), a.duration);
    pm_gfx->setCursor(96, y + static_cast<int>(shown) * 12);
    pm_gfx->print(line);
    pm_gfx->fillCircle(84, y + 4 + static_cast<int>(shown) * 12, 4, aspect_color(a.aspect));
    ++shown;
  }
}

static void draw_selected_card(uint16_t year) {
  const YearArc *arc = selected_arc();
  if (!arc) {
    draw_legend();
    return;
  }

  uint8_t sm = 1, sd = 1, em = 1, ed = 1;
  month_day_from_day(year, arc->start_day, &sm, &sd);
  month_day_from_day(year, arc->end_day, &em, &ed);

  pm_gfx->fillRoundRect(42, 300, LCD_WIDTH - 84, 64, 10, pm_gfx->color565(13, 17, 31));
  pm_gfx->drawRoundRect(42, 300, LCD_WIDTH - 84, 64, 10, aspect_color(arc->aspect));
  pm_gfx->setTextSize(1, 1);
  pm_gfx->setTextColor(pm_gfx->color565(232, 236, 248));

  char title[44];
  snprintf(title, sizeof(title), "%s", arc->title);
  pm_gfx->setCursor(58, 310);
  pm_gfx->print(title);

  char detail[54];
  snprintf(detail, sizeof(detail), "%02u/%02u-%02u/%02u  %s  orb %.1f", sm, sd, em, ed, arc->duration, arc->orb);
  pm_gfx->setCursor(58, 326);
  pm_gfx->setTextColor(pm_gfx->color565(178, 188, 214));
  pm_gfx->print(detail);

  char hint[50];
  snprintf(hint, sizeof(hint), "%u/%u  swipe up/down", static_cast<unsigned>(s_selected_arc + 1),
           static_cast<unsigned>(s_cache.arc_count));
  pm_gfx->setCursor(58, 342);
  pm_gfx->setTextColor(pm_gfx->color565(136, 150, 182));
  pm_gfx->print(hint);
}

}  // namespace

bool pm_face_year_transits_cycle_selected(int delta) {
  if (!s_cache.ready || s_cache.arc_count == 0) {
    return false;
  }
  if (s_selected_arc < 0 || s_selected_arc >= static_cast<int>(s_cache.arc_count)) {
    s_selected_arc = 0;
    return true;
  }
  const int n = static_cast<int>(s_cache.arc_count);
  int next = s_selected_arc + delta;
  next = (next % n + n) % n;
  s_selected_arc = next;
  return true;
}

bool pm_face_year_transits_selected_summary(char *buf, size_t cap) {
  if (!buf || cap == 0) {
    return false;
  }
  const YearArc *arc = selected_arc();
  if (!arc) {
    snprintf(buf, cap, "year: no arcs");
    return false;
  }
  snprintf(buf, cap, "%.28s %.10s", arc->title, arc->duration);
  return true;
}

void pm_face_year_transits_draw(const struct tm *tm_local, bool valid_local) {
  const int cx = LCD_WIDTH / 2;
  const int cy = LCD_HEIGHT / 2;
  const int R = min(LCD_WIDTH, LCD_HEIGHT) / 2;
  const int r_outer = R - 22;
  const int r_inner = 64;
  const uint16_t c_dim = pm_gfx->color565(116, 126, 150);
  const uint16_t c_ring = pm_gfx->color565(52, 58, 78);
  const uint16_t c_text = pm_gfx->color565(214, 220, 238);

  if (!valid_local || !tm_local) {
    pm_face_draw_centered_line("year transits", 186, c_text, 2, 2);
    pm_face_draw_centered_line("needs time", 214, c_dim, 1, 1);
    return;
  }

  PmBirthSpec birth = {};
  (void)pm_birth_load(&birth);
  const uint16_t year = static_cast<uint16_t>(tm_local->tm_year + 1900);
  ensure_cache(year, &birth);

  pm_gfx->drawCircle(cx, cy, r_outer, c_ring);
  pm_gfx->drawCircle(cx, cy, r_inner, c_ring);
  pm_gfx->drawCircle(cx, cy, r_outer - 46, pm_gfx->color565(34, 40, 58));
  pm_gfx->drawCircle(cx, cy, r_outer - 88, pm_gfx->color565(34, 40, 58));

  static const uint16_t k_before_month_common[] = {0,   31,  59,  90,  120, 151,
                                                   181, 212, 243, 273, 304, 334};
  const int year_days = days_in_year(year);
  for (int m = 0; m < 12; ++m) {
    int d0 = k_before_month_common[m];
    if (m >= 2 && leap_year(year)) {
      ++d0;
    }
    const float deg = day_to_deg(static_cast<float>(d0), year_days);
    const float a = pm_face_deg_to_rad(deg);
    const int x0 = cx + static_cast<int>(lrintf(cosf(a) * static_cast<float>(r_inner)));
    const int y0 = cy + static_cast<int>(lrintf(sinf(a) * static_cast<float>(r_inner)));
    const int x1 = cx + static_cast<int>(lrintf(cosf(a) * static_cast<float>(r_outer)));
    const int y1 = cy + static_cast<int>(lrintf(sinf(a) * static_cast<float>(r_outer)));
    pm_gfx->drawLine(x0, y0, x1, y1, m % 3 == 0 ? pm_gfx->color565(86, 96, 130) : c_ring);
    pm_face_draw_label_at_polar(cx, cy, r_outer + 8, a, month_label(m), c_dim);
  }

  for (size_t i = 0; i < s_cache.arc_count; ++i) {
    const YearArc &arc = s_cache.arcs[i];
    const int lane = static_cast<int>(arc.lane);
    const int r = r_outer - 18 - lane * 22;
    const float start_deg = day_to_deg(arc.start_day, year_days);
    const float end_deg = day_to_deg(arc.end_day, year_days);
    const bool selected = static_cast<int>(i) == s_selected_arc;
    draw_arc_segment(cx, cy, r, start_deg, end_deg,
                     selected ? pm_gfx->color565(255, 255, 255) : aspect_color(arc.aspect),
                     selected ? 4 : 2);
    if (selected) {
      draw_arc_segment(cx, cy, r, start_deg, end_deg, aspect_color(arc.aspect), 2);
    }
  }

  const float now_day = static_cast<float>(tm_local->tm_yday) +
                        (static_cast<float>(tm_local->tm_hour) / 24.0f) +
                        (static_cast<float>(tm_local->tm_min) / 1440.0f);
  const float now_ang = pm_face_deg_to_rad(day_to_deg(now_day, year_days));
  pm_gfx->drawLine(cx, cy,
                   cx + static_cast<int>(lrintf(cosf(now_ang) * static_cast<float>(r_outer + 2))),
                   cy + static_cast<int>(lrintf(sinf(now_ang) * static_cast<float>(r_outer + 2))),
                   pm_gfx->color565(250, 250, 255));
  pm_gfx->fillCircle(cx + static_cast<int>(lrintf(cosf(now_ang) * static_cast<float>(r_outer + 2))),
                     cy + static_cast<int>(lrintf(sinf(now_ang) * static_cast<float>(r_outer + 2))),
                     4, pm_gfx->color565(250, 250, 255));

  char title[24];
  snprintf(title, sizeof(title), "%u currents", static_cast<unsigned>(year));
  pm_face_draw_centered_line(title, cy - 14, c_text, 2, 2);
  if (!birth.valid || !s_cache.has_birth) {
    pm_face_draw_centered_line("store birth data", cy + 12, c_dim, 1, 1);
  } else if (s_cache.arc_count == 0) {
    pm_face_draw_centered_line("quiet year", cy + 12, c_dim, 1, 1);
  } else {
    char count[28];
    snprintf(count, sizeof(count), "%u named arcs", static_cast<unsigned>(s_cache.arc_count));
    pm_face_draw_centered_line(count, cy + 12, c_dim, 1, 1);
    draw_selected_card(year);
  }
}
