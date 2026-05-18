#include "faces/spectrum/pm_face_spectrum.h"

#include <Arduino.h>
#include <cmath>
#include <cstdio>

#include "faces/shared/pm_face_draw.h"
#include "faces/spectrum/pm_face_spectrum_viz.h"
#include "pin_config.h"
#include "pm_audio_analyzer.h"
#include "pm_display.h"
#include "pm_wifi_ntp.h"

static bool s_active = false;
static int s_mode = 0;
static float s_hue_spin = 0.f;

static float clock_hue_deg(void) {
  struct tm tm = {};
  if (pm_time_valid()) {
    pm_time_local(&tm);
    const int sec = tm.tm_hour * 3600 + tm.tm_min * 60 + tm.tm_sec;
    return static_cast<float>(sec) * (360.f / 86400.f);
  }
  return fmodf(static_cast<float>(millis()) * 0.02f, 360.f);
}

static void draw_mode_caption(int mode) {
  char buf[40];
  snprintf(buf, sizeof(buf), "%s %d/%d", pm_face_spectrum_mode_label(), mode + 1, PM_SPECTRUM_VIZ_COUNT);
  pm_face_draw_centered_line(buf, 24, pm_gfx->color565(120, 130, 160), 1, 1);
}

void pm_face_spectrum_on_enter(void) {
  pm_audio_analyzer_reset();
  (void)pm_audio_analyzer_mic_begin();
  pm_face_spectrum_viz_reset();
  s_active = true;
  s_mode = 0;
  s_hue_spin = 0.f;
}

void pm_face_spectrum_on_leave(void) {
  if (!s_active) {
    return;
  }
  pm_audio_analyzer_mic_end();
  s_active = false;
}

void pm_face_spectrum_cycle(int delta) {
  int v = s_mode + delta;
  v = (v % PM_SPECTRUM_VIZ_COUNT + PM_SPECTRUM_VIZ_COUNT) % PM_SPECTRUM_VIZ_COUNT;
  s_mode = v;
}

int pm_face_spectrum_mode(void) { return s_mode; }

int pm_face_spectrum_mode_count(void) { return PM_SPECTRUM_VIZ_COUNT; }

const char *pm_face_spectrum_mode_label(void) { return pm_face_spectrum_viz_label(s_mode); }

void pm_face_spectrum_tick(void) {
  if (!s_active) {
    return;
  }
  pm_audio_analyzer_tick();
  pm_face_spectrum_viz_tick(pm_audio_analyzer_get_level());
  s_hue_spin += 0.35f;
  if (s_hue_spin >= 360.f) {
    s_hue_spin -= 360.f;
  }
}

void pm_face_spectrum_draw(uint16_t bg) {
  (void)bg;

  float mix[PM_AUDIO_ANALYZER_BANDS];
  float wave[PM_AUDIO_WAVE_POINTS];
  float hist[PM_AUDIO_SPEC_HISTORY * PM_AUDIO_ANALYZER_BANDS];
  pm_audio_analyzer_get_mix(mix, PM_AUDIO_ANALYZER_BANDS);
  pm_audio_analyzer_get_waveform(wave, PM_AUDIO_WAVE_POINTS);
  pm_audio_analyzer_get_spec_history(hist, PM_AUDIO_SPEC_HISTORY, PM_AUDIO_ANALYZER_BANDS);

  const int R = (LCD_WIDTH < LCD_HEIGHT ? LCD_WIDTH : LCD_HEIGHT) / 2 - 14;
  const PmSpectrumVizCtx ctx = {
      pm_face_lcd_cx,
      pm_face_lcd_cy,
      R,
      clock_hue_deg(),
      s_hue_spin,
      mix,
      PM_AUDIO_ANALYZER_BANDS,
      wave,
      PM_AUDIO_WAVE_POINTS,
      hist,
      PM_AUDIO_SPEC_HISTORY,
      pm_audio_analyzer_get_level(),
  };

  pm_face_spectrum_viz_draw(s_mode, ctx);
  draw_mode_caption(s_mode);
}
