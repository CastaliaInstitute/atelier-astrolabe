#include "faces/astrology/pm_face_astrology.h"
#include "faces/shared/pm_face_draw.h"
#include "pm_birth_nvs.h"
#include "pm_transit.h"
#include "pm_wifi_ntp.h"
#include "pm_zodiac_glyphs.h"
#include "planet_glyphs.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include "pin_config.h"
#include "pm_display.h"

static char s_astrology_voice_msg[2200];
static char s_astrology_sys_prompt[2800];

struct TransitAspect {
  int body_a;
  int body_b;
  int aspect_deg;
  double orb;
  const char *label;
};

static double norm360(double lon) {
  lon = fmod(lon, 360.0);
  if (lon < 0.0) {
    lon += 360.0;
  }
  return lon;
}

static double aspect_distance(double a, double b) {
  double d = fabs(norm360(a) - norm360(b));
  if (d > 180.0) {
    d = 360.0 - d;
  }
  return d;
}

static const char *aspect_label(int deg) {
  switch (deg) {
    case 0:
      return "conj";
    case 60:
      return "sextile";
    case 90:
      return "square";
    case 120:
      return "trine";
    case 180:
      return "opp";
    default:
      return "aspect";
  }
}

static uint16_t aspect_color(int deg) {
  switch (deg) {
    case 0:
      return pm_gfx->color565(185, 150, 82);
    case 60:
      return pm_gfx->color565(74, 132, 166);
    case 90:
      return pm_gfx->color565(172, 76, 74);
    case 120:
      return pm_gfx->color565(82, 150, 104);
    case 180:
      return pm_gfx->color565(132, 92, 174);
    default:
      return pm_gfx->color565(110, 122, 148);
  }
}

static float angle_for_lon(double lon) {
  return static_cast<float>(pm_face_k_pi + norm360(lon) * (pm_face_k_pi / 180.0f));
}

static bool body_is_luminary(int body) {
  return body == kPmBodySun || body == kPmBodyMoon;
}

static double aspect_orb_limit(int aspect_deg, int body_a, int body_b) {
  double limit = (body_is_luminary(body_a) || body_is_luminary(body_b)) ? 5.0 : 4.0;
  if (aspect_deg == 60) {
    limit -= 0.5;
  } else if (aspect_deg == 0 || aspect_deg == 180) {
    limit += 0.5;
  }
  return limit;
}

static void insert_aspect_sorted(TransitAspect *out, int max_out, int *count, const TransitAspect &aspect) {
  if (!out || !count || max_out <= 0) {
    return;
  }
  int ins = *count;
  if (ins > max_out) {
    ins = max_out;
  }
  for (int i = 0; i < ins; ++i) {
    if (aspect.orb < out[i].orb) {
      ins = i;
      break;
    }
  }
  if (*count < max_out) {
    ++(*count);
  }
  if (ins >= max_out) {
    return;
  }
  for (int i = *count - 1; i > ins; --i) {
    out[i] = out[i - 1];
  }
  out[ins] = aspect;
}

static int build_transit_aspects(const PmTransitPositions *tp, TransitAspect *out, int max_out) {
  if (!tp || !tp->ok || !out || max_out <= 0) {
    return 0;
  }
  int count = 0;
  static const int kMajors[] = {0, 60, 90, 120, 180};
  for (int a = 0; a < kPmBodyCount; ++a) {
    for (int b = a + 1; b < kPmBodyCount; ++b) {
      const double sep = aspect_distance(tp->lon[a], tp->lon[b]);
      for (int i = 0; i < static_cast<int>(sizeof(kMajors) / sizeof(kMajors[0])); ++i) {
        const int deg = kMajors[i];
        const double orb = fabs(sep - static_cast<double>(deg));
        if (orb > aspect_orb_limit(deg, a, b)) {
          continue;
        }
        const TransitAspect aspect = {a, b, deg, orb, aspect_label(deg)};
        insert_aspect_sorted(out, max_out, &count, aspect);
        break;
      }
    }
  }
  return count;
}

static void draw_aspect_lines(const PmTransitPositions *tp, const TransitAspect *aspects, int aspect_count,
                              int cx, int cy, int r_aspect) {
  if (!tp || !tp->ok || !aspects || aspect_count <= 0) {
    return;
  }
  const int n = aspect_count < 7 ? aspect_count : 7;
  for (int i = 0; i < n; ++i) {
    const TransitAspect &a = aspects[i];
    const float aa = angle_for_lon(tp->lon[a.body_a]);
    const float ab = angle_for_lon(tp->lon[a.body_b]);
    const int x0 = cx + static_cast<int>(lrintf(cosf(aa) * static_cast<float>(r_aspect)));
    const int y0 = cy + static_cast<int>(lrintf(sinf(aa) * static_cast<float>(r_aspect)));
    const int x1 = cx + static_cast<int>(lrintf(cosf(ab) * static_cast<float>(r_aspect)));
    const int y1 = cy + static_cast<int>(lrintf(sinf(ab) * static_cast<float>(r_aspect)));
    const uint16_t col = aspect_color(a.aspect_deg);
    pm_gfx->drawLine(x0, y0, x1, y1, col);
    if (a.aspect_deg == 0) {
      pm_gfx->drawCircle((x0 + x1) / 2, (y0 + y1) / 2, 4, col);
    }
  }
}

static void draw_astrology_footer(const PmTransitPositions *tp, const TransitAspect *aspects, int aspect_count) {
  if (!tp || !tp->ok) {
    return;
  }
  const int x = 78;
  const int y = LCD_HEIGHT - 50;
  const int w = LCD_WIDTH - 156;
  const int h = 34;
  pm_gfx->fillRect(x, y, w, h, pm_gfx->color565(8, 10, 18));
  pm_gfx->drawLine(x + 20, y, x + w - 20, y, pm_gfx->color565(48, 54, 72));
  pm_face_draw_centered_line(tp->from_network ? "Castalia ephemeris" : "local approx ephemeris",
                             y + 7, pm_gfx->color565(130, 144, 174), 1, 1);
  char line[72];
  if (aspects && aspect_count > 0) {
    const TransitAspect &a = aspects[0];
    snprintf(line, sizeof(line), "%s %s %s  %.1f deg",
             pm_ephem_body_label(static_cast<PmEphemBody>(a.body_a)), a.label,
             pm_ephem_body_label(static_cast<PmEphemBody>(a.body_b)), a.orb);
  } else {
    snprintf(line, sizeof(line), "major aspects quiet");
  }
  pm_face_draw_centered_line(line, y + 22, pm_gfx->color565(214, 218, 238), 1, 1);
}

const char *pm_face_zodiac_abbr(double lon_deg) {
  static const char *const kZ[12] = {"Ar", "Ta", "Ge", "Cn", "Le", "Vi",
                                     "Li", "Sc", "Sg", "Cp", "Aq", "Pi"};
  double x = fmod(lon_deg, 360.0);
  if (x < 0) {
    x += 360.0;
  }
  const int idx = static_cast<int>(x / 30.0) % 12;
  return kZ[idx];
}



int pm_face_astrology_sign_label_radius(int r_outer) { return r_outer - 22; }



int pm_face_astrology_planet_radius(int r_outer) { return pm_face_astrology_sign_label_radius(r_outer) - 18; }



void pm_face_astrology_draw(const struct tm *tm_local, bool valid_local, int highlight_body,
                                int highlight_sign, bool pulse_chart) {
  const uint16_t c_dim = pm_gfx->color565(130, 140, 158);
  const uint16_t c_ring = pm_gfx->color565(55, 62, 78);
  const uint16_t c_spoke = pm_gfx->color565(78, 88, 108);
  const uint16_t c_lbl = pm_gfx->color565(170, 178, 195);

  struct tm utc = {};
  PmTransitPositions tp = {};
  if (valid_local) {
    pm_time_utc(&utc);
    pm_transit_compute_utc(&utc, &tp);
  }

  PmBirthSpec birth = {};
  (void)pm_birth_load(&birth);

  const int cx = LCD_WIDTH / 2;
  const int cy = LCD_HEIGHT / 2;
  const int R = min(LCD_WIDTH, LCD_HEIGHT) / 2;
  /** Chart fills the dial inside the 24h rainbow rim (rainbow inner ≈ R−9). */
  const int r_outer = R - 10;
  const int r_in = r_outer * 44 / 118;
  const int r_lab = pm_face_astrology_sign_label_radius(r_outer);
  TransitAspect aspects[10] = {};
  const int aspect_count = tp.ok ? build_transit_aspects(&tp, aspects, static_cast<int>(sizeof(aspects) / sizeof(aspects[0]))) : 0;

  if (!tp.ok) {
    pm_face_draw_centered_line("ephemeris needs", 200, c_dim, 1, 1);
    pm_face_draw_centered_line("valid UTC time", 222, c_dim, 1, 1);
  } else {
    for (int s = 0; s < 12; ++s) {
      const float a0 = static_cast<float>(s) * (pm_face_k_two_pi / 12.f) - pm_face_k_pi * 0.5f;
      const float a1 = static_cast<float>(s + 1) * (pm_face_k_two_pi / 12.f) - pm_face_k_pi * 0.5f;
      const int x0 = cx + static_cast<int>(lrintf(cosf(a0) * static_cast<float>(r_outer)));
      const int y0 = cy + static_cast<int>(lrintf(sinf(a0) * static_cast<float>(r_outer)));
      const int x1 = cx + static_cast<int>(lrintf(cosf(a1) * static_cast<float>(r_outer)));
      const int y1 = cy + static_cast<int>(lrintf(sinf(a1) * static_cast<float>(r_outer)));
      pm_gfx->drawLine(x0, y0, x1, y1, c_spoke);
      pm_gfx->drawLine(cx, cy, x0, y0, c_ring);
    }
    pm_gfx->drawCircle(cx, cy, r_outer, c_ring);
    pm_gfx->drawCircle(cx, cy, r_in, c_ring);

    if (highlight_sign >= 0 && highlight_sign < 12) {
      const uint16_t c_hi = pm_gfx->color565(72, 82, 118);
      const float a0 = static_cast<float>(highlight_sign) * (pm_face_k_two_pi / 12.f) - pm_face_k_pi * 0.5f;
      const float a1 = static_cast<float>(highlight_sign + 1) * (pm_face_k_two_pi / 12.f) - pm_face_k_pi * 0.5f;
      constexpr int k_fan = 10;
      for (int step = 0; step < k_fan; ++step) {
        const float t0 = a0 + (a1 - a0) * (static_cast<float>(step) / static_cast<float>(k_fan));
        const float t1 = a0 + (a1 - a0) * (static_cast<float>(step + 1) / static_cast<float>(k_fan));
        const int x0 = cx + static_cast<int>(lrintf(cosf(t0) * static_cast<float>(r_outer)));
        const int y0 = cy + static_cast<int>(lrintf(sinf(t0) * static_cast<float>(r_outer)));
        const int x1 = cx + static_cast<int>(lrintf(cosf(t1) * static_cast<float>(r_outer)));
        const int y1 = cy + static_cast<int>(lrintf(sinf(t1) * static_cast<float>(r_outer)));
        pm_gfx->fillTriangle(cx, cy, x0, y0, x1, y1, c_hi);
      }
      pm_gfx->drawLine(cx, cy, cx + static_cast<int>(lrintf(cosf(a0) * static_cast<float>(r_outer))),
                    cy + static_cast<int>(lrintf(sinf(a0) * static_cast<float>(r_outer))),
                    pm_gfx->color565(200, 210, 240));
      pm_gfx->drawLine(cx, cy, cx + static_cast<int>(lrintf(cosf(a1) * static_cast<float>(r_outer))),
                    cy + static_cast<int>(lrintf(sinf(a1) * static_cast<float>(r_outer))),
                    pm_gfx->color565(200, 210, 240));
    }

    static const uint16_t k_body_col[kPmBodyCount] = {
        pm_gfx->color565(255, 210, 90),  pm_gfx->color565(200, 210, 230), pm_gfx->color565(180, 180, 190),
        pm_gfx->color565(255, 190, 140), pm_gfx->color565(230, 90, 70),   pm_gfx->color565(220, 180, 120),
        pm_gfx->color565(190, 170, 140),
    };
    const int r_planets = pm_face_astrology_planet_radius(r_outer);
    draw_aspect_lines(&tp, aspects, aspect_count, cx, cy, r_planets - 28);
    const bool pulse_on = pulse_chart && ((millis() / 500u) % 2u) == 0u;
    for (int bi = 0; bi < kPmBodyCount; ++bi) {
      const double lon = tp.lon[bi];
      const float ang = angle_for_lon(lon);
      const bool hi = (highlight_body == bi);
      uint16_t col = hi ? pm_gfx->color565(255, 245, 170) : k_body_col[bi];
      if (!hi && pulse_on) {
        col = pm_gfx->color565(
            static_cast<uint8_t>(((col >> 11) & 0x1F) * 255 / 31 * 1.15f),
            static_cast<uint8_t>(((col >> 5) & 0x3F) * 255 / 63 * 1.15f),
            static_cast<uint8_t>((col & 0x1F) * 255 / 31 * 1.15f));
      }
      pm_planet_draw_at_polar(pm_gfx, cx, cy, r_planets, ang, bi, col, hi);
    }

    pm_zodiac_draw_sign_ring(pm_gfx, cx, cy, r_lab, highlight_sign, c_lbl);
    double natal_sun = 0;
    if (birth.valid && pm_transit_natal_sun_lon(&birth, &natal_sun)) {
      const float angn = angle_for_lon(natal_sun);
      const int qx = cx + static_cast<int>(lrintf(cosf(angn) * static_cast<float>(r_in - 6)));
      const int qy = cy + static_cast<int>(lrintf(sinf(angn) * static_cast<float>(r_in - 6)));
      const int q2x = cx + static_cast<int>(lrintf(cosf(angn + 0.35f) * static_cast<float>(r_in - 18)));
      const int q2y = cy + static_cast<int>(lrintf(sinf(angn + 0.35f) * static_cast<float>(r_in - 18)));
      const int q3x = cx + static_cast<int>(lrintf(cosf(angn - 0.35f) * static_cast<float>(r_in - 18)));
      const int q3y = cy + static_cast<int>(lrintf(sinf(angn - 0.35f) * static_cast<float>(r_in - 18)));
      pm_gfx->fillTriangle(qx, qy, q2x, q2y, q3x, q3y, pm_gfx->color565(120, 200, 255));
    }
    draw_astrology_footer(&tp, aspects, aspect_count);
  }
}



void pm_face_astrology_draw_voice_screen(const char *status, int highlight_body, int highlight_sign,
                                    bool pulse_chart, float thinking_progress) {
  struct tm tm = {};
  const bool valid = pm_time_valid();
  if (valid) {
    pm_time_local(&tm);
  }
  pm_gfx->fillScreen(pm_gfx->color565(12, 14, 22));
  pm_face_astrology_draw(&tm, valid, highlight_body, highlight_sign, pulse_chart);
  if (thinking_progress >= 0.f) {
    pm_face_draw_circumference_rainbow_24h(valid);
    pm_face_draw_thinking_progress_ring(thinking_progress);
  } else if (status && status[0] != '\0') {
    pm_gfx->fillRect(0, 0, LCD_WIDTH, 46, pm_gfx->color565(18, 20, 34));
    pm_face_draw_centered_line(status, 14, pm_gfx->color565(220, 200, 255), 2, 2);
  }
  pm_gfx->flush();
}



bool pm_face_astrology_build_voice_message(char *buf, size_t cap) {
  if (!buf || cap < 200) {
    return false;
  }
  if (!pm_wifi_connected() || !pm_time_valid()) {
    return false;
  }
  struct tm utc = {};
  struct tm loc = {};
  pm_time_local(&loc);
  pm_time_utc(&utc);
  PmTransitPositions tp = {};
  pm_transit_compute_utc(&utc, &tp);
  if (!tp.ok) {
    return false;
  }
  PmBirthSpec b = {};
  (void)pm_birth_load(&b);
  double nslon = 0;
  const bool has_natal = b.valid && pm_transit_natal_sun_lon(&b, &nslon);
  TransitAspect aspects[10] = {};
  const int aspect_count = build_transit_aspects(&tp, aspects, static_cast<int>(sizeof(aspects) / sizeof(aspects[0])));

  int n = snprintf(
      buf, cap,
      "Pocket Mynah transit snapshot for %04d-%02d-%02d %02d:%02d local. Tropical longitudes from %s: ",
      loc.tm_year + 1900, loc.tm_mon + 1, loc.tm_mday, loc.tm_hour, loc.tm_min,
      tp.from_network ? "Castalia ephemeris server (interpolated deg)"
                      : "local fallback ephemeris (approx deg)");
  if (n < 0 || static_cast<size_t>(n) >= cap) {
    return false;
  }
  size_t off = static_cast<size_t>(n);
  for (int i = 0; i < kPmBodyCount && off + 40 < cap; ++i) {
    const int m = snprintf(buf + off, cap - off, "%s %.1f; ", pm_ephem_body_label(static_cast<PmEphemBody>(i)),
                           tp.lon[i]);
    if (m < 0) {
      return false;
    }
    off += static_cast<size_t>(m);
  }
  if (off + 80 < cap) {
    const int m = snprintf(buf + off, cap - off, "Closest major transit aspects: ");
    if (m < 0 || static_cast<size_t>(m) >= cap - off) {
      return false;
    }
    off += static_cast<size_t>(m);
    const int aspect_n = aspect_count < 5 ? aspect_count : 5;
    if (aspect_n == 0) {
      const int q = snprintf(buf + off, cap - off, "none within watch orb; ");
      if (q < 0 || static_cast<size_t>(q) >= cap - off) {
        return false;
      }
      off += static_cast<size_t>(q);
    }
    for (int i = 0; i < aspect_n && off + 48 < cap; ++i) {
      const TransitAspect &a = aspects[i];
      const int q = snprintf(buf + off, cap - off, "%s %s %s (orb %.1f deg); ",
                             pm_ephem_body_label(static_cast<PmEphemBody>(a.body_a)), a.label,
                             pm_ephem_body_label(static_cast<PmEphemBody>(a.body_b)), a.orb);
      if (q < 0 || static_cast<size_t>(q) >= cap - off) {
        return false;
      }
      off += static_cast<size_t>(q);
    }
  }
  if (has_natal && off + 120 < cap) {
    snprintf(buf + off, cap - off,
             "Natal (local civil on this device TZ): %04u-%02u-%02u %02u:%02u — Sun ~%.1f deg (%s). ",
             b.year, b.month, b.day, b.hour, b.minute, nslon, pm_face_zodiac_abbr(nslon));
  } else if (off + 80 < cap) {
    snprintf(buf + off, cap - off, "Natal birth not stored; describe transits in general. ");
  }
  off = strlen(buf);
  if (off + 80 < cap) {
    snprintf(buf + off, cap - off, "Please deliver the spoken reading now.");
  }
  return strlen(buf) > 0;
}



bool pm_face_astrology_build_system_prompt_impl() {
  return false;  /* use pm_face_astrology_build_system_prompt() */
}



bool pm_face_astrology_build_system_prompt(char *voice_msg, size_t voice_cap, char *sys_out, size_t sys_cap) {
  if (!pm_face_astrology_build_voice_message(voice_msg, voice_cap)) {
    return false;
  }
  static const char kAstroVoiceSys[] =
      "You are a warm, articulate astrologer speaking aloud for a tiny round watch. Use tropical zodiac. "
      "Chart snapshot data is provided below. If the user asks a question, answer it using those positions; "
      "if they did not ask a question, give ONE flowing mini-reading (under 90 seconds spoken) about today's "
      "transits versus their natal Sun and anything else notable. "
      "No medical or legal advice; reflective insight only, not deterministic fate. "
      "Do not claim arc-minute precision from the numbers. "
      "Do not use asterisk stage directions or emotes (e.g. *smiles*); output only words to be spoken aloud.";
  const int n = snprintf(sys_out, sys_cap, "%s\n\nChart snapshot:\n%s", kAstroVoiceSys, voice_msg);
  return n > 0 && static_cast<size_t>(n) < sys_cap;
}
