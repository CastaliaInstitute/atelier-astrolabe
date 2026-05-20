#include "faces/moon/pm_face_moon.h"
#include "faces/shared/pm_face_draw.h"
#include "pm_moon_draw.h"
#include "pm_transit.h"
#include "pm_wifi_ntp.h"
#include <cmath>
#include <cstdio>
#include <ctime>
#include "pin_config.h"
#include "pm_display.h"

const char *pm_face_moon_phase_name(double el_deg) {
  int oct = static_cast<int>(el_deg / 45.0) % 8;
  if (oct < 0) {
    oct += 8;
  }
  static const char *const k[] = {"New moon",      "Waxing crescent", "First quarter", "Waxing gibbous",
                                  "Full moon",     "Waning gibbous",  "Last quarter",  "Waning crescent"};
  return k[oct];
}



void pm_face_moon_draw(const struct tm *tm_local, bool valid_local) {
  const uint16_t c_dim = pm_gfx->color565(150, 160, 178);
  pm_gfx->fillScreen(pm_gfx->color565(8, 10, 18));
  if (!valid_local) {
    pm_face_draw_centered_line("need NTP time", 220, c_dim, 2, 2);
    return;
  }
  struct tm utc = {};
  pm_time_utc(&utc);
  PmTransitPositions tp = {};
  pm_transit_compute_utc(&utc, &tp);
  float illum = 0.5f;
  bool waxing = true;
  double el = 0.0;
  if (!pm_moon_phase_from_transit(&tp, &illum, &waxing, &el)) {
    pm_face_draw_centered_line("ephemeris", 220, c_dim, 2, 2);
    return;
  }
  const int cx = LCD_WIDTH / 2;
  const int cy = LCD_HEIGHT / 2;
  /** Disk inside 24h rainbow (outer R−4, inner R−9); same inset as astrology chart. */
  const int R = min(LCD_WIDTH, LCD_HEIGHT) / 2;
  const int r = R - 14;
  pm_moon_draw_disk(pm_gfx, cx, cy, r, illum, waxing, false);
  pm_face_draw_circumference_rainbow_24h(valid_local);
  (void)tm_local;
}

void pm_face_moon_draw_voice_screen(const char *status, float thinking_progress) {
  struct tm tm = {};
  const bool valid = pm_time_valid();
  if (valid) {
    pm_time_local(&tm);
  }
  pm_face_moon_draw(&tm, valid);
  if (thinking_progress >= 0.f) {
    pm_face_draw_thinking_progress_ring(thinking_progress);
  } else if (status && status[0] != '\0') {
    pm_gfx->fillRect(0, 0, LCD_WIDTH, 44, pm_gfx->color565(10, 12, 22));
    pm_face_draw_centered_line(status, 12, pm_gfx->color565(210, 215, 235), 1, 1);
  }
  pm_gfx->flush();
}

static const char kMoonFortuneSys[] =
    "You are a lunar guide on a small round pocket watch, speaking like a gentle fortune teller reading "
    "moonlit omens from a brass astrolabe. Speak only words to be heard aloud—no stage directions or "
    "emotes. Use the sky data in the user message. Deliver today's lunar fortune: attuned to the current "
    "Moon phase, warm, reflective, and concise (under 75 seconds spoken). Offer one omen, one counsel, and "
    "one vivid night-sky image. Not deterministic fate; no medical, legal, or financial advice.";

bool pm_face_moon_build_fortune_message(char *buf, size_t cap) {
  if (!buf || cap < 200 || !pm_wifi_connected() || !pm_time_valid()) {
    return false;
  }
  struct tm loc = {};
  pm_time_local(&loc);
  struct tm utc = {};
  pm_time_utc(&utc);
  PmTransitPositions tp = {};
  pm_transit_compute_utc(&utc, &tp);
  if (!tp.ok) {
    return false;
  }
  float illum = 0.5f;
  bool wax = true;
  double el = 0.0;
  if (!pm_moon_phase_from_transit(&tp, &illum, &wax, &el)) {
    return false;
  }
  const char *nm = pm_face_moon_phase_name(el);
  const int n = snprintf(
      buf, cap,
      "Astrolabe pocket watch. Local calendar day %04d-%02d-%02d at %02d:%02d. Sun-Moon elongation "
      "~%.0f deg; Moon ~%d%% illuminated, %s; phase name \"%s\". The wearer tapped the Moon face for "
      "today's lunar fortune. Speak the fortune now in 4-6 short sentences: the day's mood under this "
      "Moon, one reflection to carry, one image or metaphor. Poetic and grounded.",
      loc.tm_year + 1900, loc.tm_mon + 1, loc.tm_mday, loc.tm_hour, loc.tm_min, el,
      static_cast<int>(lrintf(illum * 100.f)), wax ? "waxing" : "waning", nm);
  return n > 80 && static_cast<size_t>(n) < cap;
}

bool pm_face_moon_build_fortune_system_prompt(char *out, size_t cap) {
  if (!out || cap < 32 || !pm_time_valid()) {
    return false;
  }
  struct tm utc = {};
  pm_time_utc(&utc);
  PmTransitPositions tp = {};
  pm_transit_compute_utc(&utc, &tp);
  if (!tp.ok) {
    return false;
  }
  float illum = 0.5f;
  bool wax = true;
  double el = 0.0;
  if (!pm_moon_phase_from_transit(&tp, &illum, &wax, &el)) {
    return false;
  }
  const char *nm = pm_face_moon_phase_name(el);
  const int n = snprintf(out, cap, "%s\n\nMoon now: %s, %d%% lit, %s.", kMoonFortuneSys, nm,
                         static_cast<int>(lrintf(illum * 100.f)), wax ? "waxing" : "waning");
  return n > 0 && static_cast<size_t>(n) < cap;
}

bool pm_face_moon_build_system_prompt(char *out, size_t cap) {
  if (!out || cap < 8) {
    return false;
  }
  snprintf(out, cap,
           "Moon context for a spoken answer. User asked via microphone. Keep reply brief for audio.");
  return out[0] != '\0';
}
