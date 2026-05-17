#include "faces/pm_faces.h"

#include <Arduino_GFX_Library.h>
#include <cmath>
#include <ctime>

#include "faces/apocalypso/pm_face_apocalypso.h"
#include "faces/astrology/pm_face_astrology.h"
#include "faces/calcifer/pm_face_calcifer.h"
#include "faces/castalia/pm_face_castalia.h"
#include "faces/classic_analog/pm_face_classic_analog.h"
#include "faces/digital/pm_face_digital.h"
#include "faces/moon/pm_face_moon.h"
#include "faces/shared/pm_face_draw.h"
#include "faces/spotify/pm_face_spotify.h"
#include "pm_config.h"
#include "pm_display.h"
#include "pm_wifi_ntp.h"

extern char g_gesture_banner[44];

static ClockFace s_clock_face = ClockFace::ClassicAnalog;
static uint16_t s_clock_bg565 = 0;
static int s_analog_saved_local_h = -1;
static int s_analog_saved_local_m = -1;

ClockFace pm_faces_current(void) { return s_clock_face; }
void pm_faces_set(ClockFace face) { s_clock_face = face; }

void pm_faces_cycle(int delta) {
  int v = static_cast<int>(s_clock_face) + delta;
  const int n = static_cast<int>(ClockFace::kNumFaces);
  v = (v % n + n) % n;
  s_clock_face = static_cast<ClockFace>(v);
}



void pm_faces_draw(float thinking_progress) {
  struct tm tm = {};
  int sec_of_day_for_hue = 0;
  if (pm_time_valid()) {
    pm_time_local(&tm);
    sec_of_day_for_hue = tm.tm_hour * 3600 + tm.tm_min * 60 + tm.tm_sec;
  }
  const float hue =
      pm_time_valid() ? static_cast<float>(sec_of_day_for_hue) * (360.0f / 86400.0f)
                       : fmodf(static_cast<float>(millis()) * 0.0015f, 360.0f);
  const uint16_t bg_hsv = pm_face_color565_from_hsv(pm_gfx, hue, pm_face_hsv_s, pm_face_hsv_v);
  const uint16_t bg = bg_hsv;
  pm_gfx->fillScreen(bg);

  switch (s_clock_face) {
    case ClockFace::ClassicAnalog:
      pm_face_classic_analog_draw(bg, &tm, pm_time_valid());
      break;
    case ClockFace::Apocalypso:
      pm_face_apocalypso_draw(&tm, pm_time_valid());
      break;
    case ClockFace::DigitalLocal:
      pm_face_digital_draw(&tm, pm_time_valid());
      break;
    case ClockFace::Spotify:
      pm_face_spotify_draw();
      break;
    case ClockFace::Astrology:
      pm_face_astrology_draw(&tm, pm_time_valid(), -1, -1, false);
      break;
    case ClockFace::Moon:
      pm_face_moon_draw(&tm, pm_time_valid());
      break;
    case ClockFace::CalciferCountdown:
      pm_face_calcifer_draw();
      break;
    case ClockFace::Castalia:
      pm_face_castalia_draw();
      break;
    default:
      break;
  }

  const int banner_y = (s_clock_face == ClockFace::Apocalypso || s_clock_face == ClockFace::Spotify ||
                        s_clock_face == ClockFace::Astrology || s_clock_face == ClockFace::Moon ||
                        s_clock_face == ClockFace::CalciferCountdown || s_clock_face == ClockFace::Castalia)
                           ? 352
                           : 320;
  if (MYNAH_DEBUG_GESTURES && g_gesture_banner[0] != '\0') {
    pm_face_draw_centered_line(g_gesture_banner, banner_y, pm_gfx->color565(255, 220, 160), 1, 1);
  }

  /** Rainbow annulus last (Moon draws its own; skip Castalia — QR repaint was tripping WDT/stack). */
  if (s_clock_face != ClockFace::Castalia && s_clock_face != ClockFace::Moon) {
    pm_face_draw_circumference_rainbow_24h(pm_time_valid());
    if (thinking_progress >= 0.f) {
      pm_face_draw_thinking_progress_ring(thinking_progress);
    }
  }
  s_clock_bg565 = bg;
  if (pm_time_valid()) {
    s_analog_saved_local_h = tm.tm_hour;
    s_analog_saved_local_m = tm.tm_min;
  }
  pm_gfx->flush();
}



bool pm_faces_banner_low(void) {
  const ClockFace f = s_clock_face;
  return f == ClockFace::Apocalypso || f == ClockFace::Spotify || f == ClockFace::Astrology ||
         f == ClockFace::Moon || f == ClockFace::CalciferCountdown || f == ClockFace::Castalia;
}

uint16_t pm_faces_last_bg565(void) { return s_clock_bg565; }

bool pm_faces_local_hm_changed(int hour, int min) {
  if (s_clock_face == ClockFace::Castalia) {
    return false;
  }
  return s_analog_saved_local_h < 0 || hour != s_analog_saved_local_h || min != s_analog_saved_local_m;
}

bool pm_faces_is_commonplace_home(void) {
  return s_clock_face == ClockFace::ClassicAnalog;
}
