#include "faces/pm_faces.h"

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <cmath>
#include <ctime>

#include "faces/pm_face_registry.h"
#include "faces/pm_face_scratch.h"
#include "faces/shared/pm_face_draw.h"
#include "pm_config.h"
#include "pm_display.h"
#include "pm_heap.h"
#include "pm_settings.h"
#include "pm_wifi_ntp.h"

extern char g_gesture_banner[44];

static ClockFace s_clock_face = ClockFace::ClassicAnalog;
static uint16_t s_clock_bg565 = 0;
static int s_analog_saved_local_h = -1;
static int s_analog_saved_local_m = -1;

static float pm_faces_home_hue_deg(void) {
  struct tm tm = {};
  int sec_of_day_for_hue = 0;
  if (pm_time_valid()) {
    pm_time_local(&tm);
    sec_of_day_for_hue = tm.tm_hour * 3600 + tm.tm_min * 60 + tm.tm_sec;
    return static_cast<float>(sec_of_day_for_hue) * (360.0f / 86400.0f);
  }
  return fmodf(static_cast<float>(millis()) * 0.0015f, 360.0f);
}

static bool pm_faces_skip_in_dial(ClockFace face) {
  return pm_face_registry_has_flag(face, kPmFaceHiddenFromDial);
}

static const PmFaceDescriptor *pm_faces_current_descriptor(void) {
  return pm_face_registry_find(s_clock_face);
}

static void pm_faces_transition_to(ClockFace face) {
  const ClockFace prev = s_clock_face;
  if (prev == face) {
    return;
  }

  const PmFaceDescriptor *prev_descriptor = pm_face_registry_find(prev);
  pm_heap_trace("face-leave", static_cast<int>(prev));
  if (prev_descriptor && prev_descriptor->on_leave) {
    prev_descriptor->on_leave(face);
  }
  pm_face_scratch_reset();

  s_clock_face = face;

  const PmFaceDescriptor *next_descriptor = pm_face_registry_find(face);
  pm_heap_trace("face-enter", static_cast<int>(face));
  if (next_descriptor && next_descriptor->on_enter) {
    next_descriptor->on_enter(prev);
  }
  pm_heap_trace("face-transition", static_cast<int>(face));
}

ClockFace pm_faces_current(void) { return s_clock_face; }

void pm_faces_set(ClockFace face) {
  if (face == ClockFace::Castalia) {
    face = ClockFace::Settings;
    pm_settings_set_page(SettingsPage::Castalia);
  }
  pm_faces_transition_to(face);
}

void pm_faces_open_settings(void) {
  pm_settings_set_page(SettingsPage::WiFi);
  pm_faces_set(ClockFace::Settings);
}

bool pm_faces_castalia_active(void) {
  return s_clock_face == ClockFace::Settings && pm_settings_page() == SettingsPage::Castalia;
}

bool pm_faces_tick(uint32_t now_ms) {
  const PmFaceDescriptor *face = pm_faces_current_descriptor();
  if (!face || !face->tick) {
    return false;
  }
  const PmFaceTickContext ctx = {now_ms};
  return face->tick(ctx);
}

void pm_faces_cycle(int delta) {
  if (s_clock_face == ClockFace::Settings) {
    return;
  }
  int v = static_cast<int>(s_clock_face);
  const int n = static_cast<int>(ClockFace::kNumFaces);
  do {
    v = (v + delta + n) % n;
  } while (pm_faces_skip_in_dial(static_cast<ClockFace>(v)));
  pm_faces_transition_to(static_cast<ClockFace>(v));
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
  uint16_t bg = bg_hsv;

  const PmFaceDescriptor *face = pm_faces_current_descriptor();
  if (!pm_face_has_flag(face, kPmFaceDrawsOwnBackground)) {
#if MYNAH_HUE_HOME_ONLY
    if (s_clock_face == ClockFace::ClassicAnalog) {
      bg = pm_face_draw_home_gem_glow(hue);
    } else {
      pm_gfx->fillScreen(bg);
    }
#else
    pm_gfx->fillScreen(bg);
#endif
  }

  PmFaceDrawContext ctx = {bg, &tm, pm_time_valid(), pm_time_valid() ? tm.tm_hour : 0,
                           pm_time_valid() ? tm.tm_min : 0};
  if (face && face->draw) {
    face->draw(ctx);
  }
  static ClockFace s_last_draw_trace_face = ClockFace::kNumFaces;
  static uint32_t s_last_draw_trace_ms = 0;
  const uint32_t now_ms = millis();
  if (s_last_draw_trace_face != s_clock_face || now_ms - s_last_draw_trace_ms >= 10000u) {
    s_last_draw_trace_face = s_clock_face;
    s_last_draw_trace_ms = now_ms;
    pm_heap_trace("face-draw", static_cast<int>(s_clock_face));
  }

  const int banner_y = pm_face_has_flag(face, kPmFaceLowGestureBanner) ? 352 : 320;
  if (MYNAH_DEBUG_GESTURES && g_gesture_banner[0] != '\0') {
    pm_face_draw_centered_line(g_gesture_banner, banner_y, pm_gfx->color565(255, 220, 160), 1, 1);
  }

  if (!pm_faces_castalia_active() && !pm_face_has_flag(face, kPmFaceSkipRainbow)) {
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

void pm_faces_draw_home_gem_pulse(void) {
#if MYNAH_HUE_HOME_ONLY
  if (s_clock_face != ClockFace::ClassicAnalog) {
    return;
  }
  const float hue = pm_faces_home_hue_deg();
  pm_face_draw_home_gem_breath_only(hue);
  pm_gfx->flush();
#else
  (void)0;
#endif
}

bool pm_faces_banner_low(void) {
  return pm_face_registry_has_flag(s_clock_face, kPmFaceLowGestureBanner);
}

uint16_t pm_faces_last_bg565(void) { return s_clock_bg565; }

bool pm_faces_local_hm_changed(int hour, int min) {
  if (pm_face_registry_has_flag(s_clock_face, kPmFaceNoMinuteRedraw)) {
    return false;
  }
  return s_analog_saved_local_h < 0 || hour != s_analog_saved_local_h || min != s_analog_saved_local_m;
}

bool pm_faces_is_commonplace_home(void) {
  return s_clock_face == ClockFace::ClassicAnalog;
}

bool pm_faces_voice_input_enabled(void) {
  return !pm_face_registry_has_flag(s_clock_face, kPmFaceDisableVoiceInput);
}
