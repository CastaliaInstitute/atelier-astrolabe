#include "faces/astrology/pm_face_astrology.h"
#include "faces/shared/pm_face_draw.h"
#include "pm_birth_nvs.h"
#include "pm_rhythms.h"
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
    const bool pulse_on = pulse_chart && ((millis() / 500u) % 2u) == 0u;
    for (int bi = 0; bi < kPmBodyCount; ++bi) {
      const double lon = tp.lon[bi];
      const float ang = static_cast<float>(pm_face_k_pi + lon * (pm_face_k_pi / 180.0f));
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
      const float angn = static_cast<float>(pm_face_k_pi + natal_sun * (pm_face_k_pi / 180.0f));
      const int qx = cx + static_cast<int>(lrintf(cosf(angn) * static_cast<float>(r_in - 6)));
      const int qy = cy + static_cast<int>(lrintf(sinf(angn) * static_cast<float>(r_in - 6)));
      const int q2x = cx + static_cast<int>(lrintf(cosf(angn + 0.35f) * static_cast<float>(r_in - 18)));
      const int q2y = cy + static_cast<int>(lrintf(sinf(angn + 0.35f) * static_cast<float>(r_in - 18)));
      const int q3x = cx + static_cast<int>(lrintf(cosf(angn - 0.35f) * static_cast<float>(r_in - 18)));
      const int q3y = cy + static_cast<int>(lrintf(sinf(angn - 0.35f) * static_cast<float>(r_in - 18)));
      pm_gfx->fillTriangle(qx, qy, q2x, q2y, q3x, q3y, pm_gfx->color565(120, 200, 255));
    }
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

  int n = snprintf(
      buf, cap,
      "Pocket Mynah transit snapshot for %04d-%02d-%02d %02d:%02d local. Tropical longitudes (approx deg): ",
      loc.tm_year + 1900, loc.tm_mon + 1, loc.tm_mday, loc.tm_hour, loc.tm_min);
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
  if (pm_rhythms_has_cached_daily_card()) {
    return pm_rhythms_build_tts_message(voice_msg, voice_cap) &&
           pm_rhythms_build_tts_system_prompt(sys_out, sys_cap);
  }
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
