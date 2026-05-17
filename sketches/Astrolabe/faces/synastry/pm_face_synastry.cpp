#include "faces/synastry/pm_face_synastry.h"

#include <Arduino_GFX_Library.h>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "faces/astrology/pm_face_astrology.h"
#include "faces/astrology/pm_zodiac_glyphs.h"
#include "faces/shared/pm_face_draw.h"
#include "pin_config.h"
#include "pm_birth_nvs.h"
#include "pm_chart_profiles.h"
#include "pm_display.h"
#include "pm_transit.h"
#include "pm_wifi_ntp.h"

struct SynastryAspect {
  int user_body;
  int target_body;
  int aspect_deg;
  double orb;
  const char *label;
};

static int s_cached_slot = -999;
static uint32_t s_last_cache_try_ms = 0;
static bool s_cache_ok = false;
static PmBirthSpec s_user_birth = {};
static PmChartProfile s_target_profile = {};
static PmTransitPositions s_user_pos = {};
static PmTransitPositions s_target_pos = {};
static SynastryAspect s_aspects[10] = {};
static int s_aspect_count = 0;

static double norm360(double v) {
  v = fmod(v, 360.0);
  if (v < 0.0) {
    v += 360.0;
  }
  return v;
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
      return pm_gfx->color565(255, 230, 150);
    case 60:
      return pm_gfx->color565(130, 220, 255);
    case 90:
      return pm_gfx->color565(255, 120, 110);
    case 120:
      return pm_gfx->color565(145, 235, 175);
    case 180:
      return pm_gfx->color565(210, 150, 255);
    default:
      return pm_gfx->color565(170, 180, 200);
  }
}

static float angle_for_lon(double lon) {
  return static_cast<float>(pm_face_k_pi + norm360(lon) * (pm_face_k_pi / 180.0f));
}

static bool append_text(char *buf, size_t cap, size_t *off, const char *fmt, ...) {
  if (!buf || !off || *off >= cap) {
    return false;
  }
  va_list ap;
  va_start(ap, fmt);
  const int n = vsnprintf(buf + *off, cap - *off, fmt, ap);
  va_end(ap);
  if (n < 0 || static_cast<size_t>(n) >= cap - *off) {
    return false;
  }
  *off += static_cast<size_t>(n);
  return true;
}

static void rebuild_aspects(void) {
  s_aspect_count = 0;
  static const int kMajors[] = {0, 60, 90, 120, 180};
  for (int ui = 0; ui < kPmBodyCount; ++ui) {
    for (int ti = 0; ti < kPmBodyCount; ++ti) {
      const double sep = aspect_distance(s_user_pos.lon[ui], s_target_pos.lon[ti]);
      for (int ai = 0; ai < static_cast<int>(sizeof(kMajors) / sizeof(kMajors[0])); ++ai) {
        const double orb = fabs(sep - static_cast<double>(kMajors[ai]));
        if (orb > 4.5) {
          continue;
        }
        SynastryAspect a = {ui, ti, kMajors[ai], orb, aspect_label(kMajors[ai])};
        int ins = s_aspect_count;
        if (ins > static_cast<int>(sizeof(s_aspects) / sizeof(s_aspects[0]))) {
          ins = static_cast<int>(sizeof(s_aspects) / sizeof(s_aspects[0]));
        }
        for (int k = 0; k < ins; ++k) {
          if (a.orb < s_aspects[k].orb) {
            ins = k;
            break;
          }
        }
        const int max_aspects = static_cast<int>(sizeof(s_aspects) / sizeof(s_aspects[0]));
        if (s_aspect_count < max_aspects) {
          ++s_aspect_count;
        }
        if (ins < max_aspects) {
          for (int k = s_aspect_count - 1; k > ins; --k) {
            s_aspects[k] = s_aspects[k - 1];
          }
          s_aspects[ins] = a;
        }
        break;
      }
    }
  }
}

static bool ensure_chart_cache(bool force) {
  pm_chart_profiles_ensure_demo_seed();
  const int slot = pm_chart_profiles_active_slot();
  const uint32_t now = millis();
  if (!force && s_cache_ok && slot == s_cached_slot) {
    return true;
  }
  if (!force && !s_cache_ok && slot == s_cached_slot && s_last_cache_try_ms != 0 &&
      (now - s_last_cache_try_ms) < 30000u) {
    return false;
  }
  s_last_cache_try_ms = now;
  s_cache_ok = false;
  s_cached_slot = slot;
  memset(&s_user_birth, 0, sizeof(s_user_birth));
  memset(&s_target_profile, 0, sizeof(s_target_profile));
  memset(&s_user_pos, 0, sizeof(s_user_pos));
  memset(&s_target_pos, 0, sizeof(s_target_pos));
  s_aspect_count = 0;
  if (slot < 0 || !pm_birth_load(&s_user_birth) || !pm_chart_profile_get(slot, &s_target_profile)) {
    return false;
  }
  PmBirthSpec target_birth = {};
  if (!pm_chart_profile_to_birth(&s_target_profile, &target_birth)) {
    return false;
  }
  if (!pm_transit_birth_positions(&s_user_birth, &s_user_pos) ||
      !pm_transit_birth_positions(&target_birth, &s_target_pos)) {
    return false;
  }
  rebuild_aspects();
  s_cache_ok = true;
  return true;
}

static void draw_body_ring(const PmTransitPositions *pos, int cx, int cy, int radius, uint16_t color,
                           bool target_ring) {
  if (!pos || !pos->ok) {
    return;
  }
  for (int bi = 0; bi < kPmBodyCount; ++bi) {
    uint16_t col = color;
    if (target_ring && bi == kPmBodySun) {
      col = pm_gfx->color565(255, 220, 110);
    } else if (!target_ring && bi == kPmBodySun) {
      col = pm_gfx->color565(115, 205, 255);
    }
    pm_planet_draw_at_polar(pm_gfx, cx, cy, radius, angle_for_lon(pos->lon[bi]), bi, col, false);
  }
}

static void draw_aspect_lines(int cx, int cy, int r_user, int r_target) {
  for (int i = 0; i < s_aspect_count && i < 7; ++i) {
    const SynastryAspect &a = s_aspects[i];
    const float au = angle_for_lon(s_user_pos.lon[a.user_body]);
    const float at = angle_for_lon(s_target_pos.lon[a.target_body]);
    const int x0 = cx + static_cast<int>(lrintf(cosf(au) * static_cast<float>(r_user)));
    const int y0 = cy + static_cast<int>(lrintf(sinf(au) * static_cast<float>(r_user)));
    const int x1 = cx + static_cast<int>(lrintf(cosf(at) * static_cast<float>(r_target)));
    const int y1 = cy + static_cast<int>(lrintf(sinf(at) * static_cast<float>(r_target)));
    pm_gfx->drawLine(x0, y0, x1, y1, aspect_color(a.aspect_deg));
  }
}

static void draw_synastry_chart(void) {
  const uint16_t c_ring = pm_gfx->color565(48, 56, 76);
  const uint16_t c_spoke = pm_gfx->color565(62, 72, 96);
  const uint16_t c_lbl = pm_gfx->color565(170, 180, 205);
  const int cx = LCD_WIDTH / 2;
  const int cy = LCD_HEIGHT / 2;
  const int R = min(LCD_WIDTH, LCD_HEIGHT) / 2;
  const int r_outer = R - 12;
  const int r_target = r_outer - 42;
  const int r_user = r_outer - 82;
  const int r_inner = r_user - 24;

  for (int s = 0; s < 12; ++s) {
    const float a = static_cast<float>(s) * (pm_face_k_two_pi / 12.f) - pm_face_k_pi * 0.5f;
    const int x0 = cx + static_cast<int>(lrintf(cosf(a) * static_cast<float>(r_inner)));
    const int y0 = cy + static_cast<int>(lrintf(sinf(a) * static_cast<float>(r_inner)));
    const int x1 = cx + static_cast<int>(lrintf(cosf(a) * static_cast<float>(r_outer)));
    const int y1 = cy + static_cast<int>(lrintf(sinf(a) * static_cast<float>(r_outer)));
    pm_gfx->drawLine(x0, y0, x1, y1, c_spoke);
  }
  pm_gfx->drawCircle(cx, cy, r_outer, c_ring);
  pm_gfx->drawCircle(cx, cy, r_target + 18, c_ring);
  pm_gfx->drawCircle(cx, cy, r_user + 18, c_ring);
  pm_gfx->drawCircle(cx, cy, r_inner, c_ring);

  draw_aspect_lines(cx, cy, r_user, r_target);
  draw_body_ring(&s_target_pos, cx, cy, r_target, pm_gfx->color565(235, 205, 255), true);
  draw_body_ring(&s_user_pos, cx, cy, r_user, pm_gfx->color565(135, 215, 255), false);
  pm_zodiac_draw_sign_ring(pm_gfx, cx, cy, r_outer - 22, -1, c_lbl);

  pm_gfx->fillRect(0, 0, LCD_WIDTH, 44, pm_gfx->color565(10, 12, 22));
  pm_face_draw_centered_line("synastry", 8, pm_gfx->color565(210, 220, 255), 2, 2);
  pm_face_draw_centered_line(s_target_profile.name, 30, pm_gfx->color565(245, 230, 255), 1, 1);

  pm_gfx->fillRect(0, LCD_HEIGHT - 58, LCD_WIDTH, 58, pm_gfx->color565(10, 12, 22));
  char line[76];
  if (s_aspect_count > 0) {
    const SynastryAspect &a = s_aspects[0];
    snprintf(line, sizeof(line), "you %s %s %s %.1f",
             pm_ephem_body_label(static_cast<PmEphemBody>(a.user_body)), a.label,
             pm_ephem_body_label(static_cast<PmEphemBody>(a.target_body)), a.orb);
  } else {
    snprintf(line, sizeof(line), "dual wheel: you + %s", s_target_profile.name);
  }
  pm_face_draw_centered_line(line, LCD_HEIGHT - 48, pm_gfx->color565(225, 226, 238), 1, 1);
  snprintf(line, sizeof(line), "up/down target  PWR ask  BOOT brief");
  pm_face_draw_centered_line(line, LCD_HEIGHT - 25, pm_gfx->color565(150, 160, 182), 1, 1);
}

void pm_face_synastry_draw(const struct tm *tm_local, bool valid_local) {
  (void)tm_local;
  (void)valid_local;
  if (!ensure_chart_cache(false)) {
    const uint16_t c_dim = pm_gfx->color565(145, 154, 178);
    pm_face_draw_centered_line("synastry", 138, pm_gfx->color565(210, 220, 255), 2, 2);
    PmChartProfile tmp = {};
    if (pm_chart_profile_count() <= 0) {
      pm_face_draw_centered_line("seeding profiles", 190, c_dim, 1, 1);
    } else if (!pm_birth_load(&s_user_birth)) {
      pm_face_draw_centered_line("set your birth chart", 190, c_dim, 1, 1);
      pm_face_draw_centered_line("serial: birth YYYY MM DD HH MI", 214, c_dim, 1, 1);
    } else if (!pm_chart_profiles_active(&tmp)) {
      pm_face_draw_centered_line("no target profiles", 190, c_dim, 1, 1);
    } else {
      pm_face_draw_centered_line("chart unavailable", 190, c_dim, 1, 1);
      pm_face_draw_centered_line("ephemeris retrying", 214, c_dim, 1, 1);
    }
    return;
  }
  draw_synastry_chart();
}

void pm_face_synastry_draw_voice_screen(const char *status, float thinking_progress) {
  struct tm tm = {};
  const bool valid = pm_time_valid();
  if (valid) {
    pm_time_local(&tm);
  }
  pm_gfx->fillScreen(pm_gfx->color565(10, 12, 22));
  pm_face_synastry_draw(&tm, valid);
  if (thinking_progress >= 0.f) {
    pm_face_draw_circumference_rainbow_24h(valid);
    pm_face_draw_thinking_progress_ring(thinking_progress);
  } else if (status && status[0] != '\0') {
    pm_gfx->fillRect(0, 0, LCD_WIDTH, 44, pm_gfx->color565(16, 18, 32));
    pm_face_draw_centered_line(status, 13, pm_gfx->color565(230, 210, 255), 2, 2);
  }
  pm_gfx->flush();
}

bool pm_face_synastry_cycle_target(int delta) {
  int slot = -1;
  PmChartProfile profile = {};
  if (!pm_chart_profiles_cycle_active(delta, &slot, &profile)) {
    return false;
  }
  s_cached_slot = -999;
  s_cache_ok = false;
  return true;
}

bool pm_face_synastry_build_voice_message(char *buf, size_t cap) {
  if (!buf || cap < 512) {
    return false;
  }
  if (!ensure_chart_cache(true)) {
    return false;
  }
  size_t off = 0;
  if (!append_text(buf, cap, &off,
                   "Pocket Mynah synastry snapshot. User birth: %04u-%02u-%02u %02u:%02u local at %s. "
                   "Target: %s (%s), %04u-%02u-%02u %02u:%02u local at %s. ",
                   s_user_birth.year, s_user_birth.month, s_user_birth.day, s_user_birth.hour,
                   s_user_birth.minute, s_user_birth.place[0] ? s_user_birth.place : "unknown place",
                   s_target_profile.name, pm_chart_role_label(s_target_profile.role), s_target_profile.year,
                   s_target_profile.month, s_target_profile.day, s_target_profile.hour, s_target_profile.minute,
                   s_target_profile.place)) {
    return false;
  }
  if (!append_text(buf, cap, &off, "User longitudes: ")) {
    return false;
  }
  for (int i = 0; i < kPmBodyCount; ++i) {
    if (!append_text(buf, cap, &off, "%s %.1f %s; ",
                     pm_ephem_body_label(static_cast<PmEphemBody>(i)), s_user_pos.lon[i],
                     pm_face_zodiac_abbr(s_user_pos.lon[i]))) {
      return false;
    }
  }
  if (!append_text(buf, cap, &off, "Target longitudes: ")) {
    return false;
  }
  for (int i = 0; i < kPmBodyCount; ++i) {
    if (!append_text(buf, cap, &off, "%s %.1f %s; ",
                     pm_ephem_body_label(static_cast<PmEphemBody>(i)), s_target_pos.lon[i],
                     pm_face_zodiac_abbr(s_target_pos.lon[i]))) {
      return false;
    }
  }
  if (!append_text(buf, cap, &off, "Closest cross-chart aspects: ")) {
    return false;
  }
  const int n = s_aspect_count < 8 ? s_aspect_count : 8;
  for (int i = 0; i < n; ++i) {
    const SynastryAspect &a = s_aspects[i];
    if (!append_text(buf, cap, &off, "user %s %s target %s (orb %.1f deg); ",
                     pm_ephem_body_label(static_cast<PmEphemBody>(a.user_body)), a.label,
                     pm_ephem_body_label(static_cast<PmEphemBody>(a.target_body)), a.orb)) {
      return false;
    }
  }
  if (n == 0 && !append_text(buf, cap, &off, "none within the watch orb; ")) {
    return false;
  }
  return append_text(buf, cap, &off, "Please deliver the spoken synastry reading now.");
}

bool pm_face_synastry_build_system_prompt(char *voice_msg, size_t voice_cap, char *sys_out, size_t sys_cap) {
  if (!pm_face_synastry_build_voice_message(voice_msg, voice_cap)) {
    return false;
  }
  static const char kSynastryVoiceSys[] =
      "You are a warm, articulate astrologer speaking aloud for a tiny round watch. Use tropical zodiac. "
      "A synastry snapshot is provided below for the user's birth chart and one selected partner/family profile. "
      "If the user asks a question, answer it using the chart data; if they did not ask a question, give one "
      "flowing relationship highlight under 90 seconds spoken. Emphasize patterns, care, and agency rather than "
      "fixed fate. Avoid medical, legal, or deterministic claims. Do not claim arc-minute precision from these "
      "numbers. Output only words to be spoken aloud; no asterisk stage directions or emotes.";
  const int n = snprintf(sys_out, sys_cap, "%s\n\nSynastry chart snapshot:\n%s", kSynastryVoiceSys, voice_msg);
  return n > 0 && static_cast<size_t>(n) < sys_cap;
}
