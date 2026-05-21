#include "faces/settings/pm_face_settings_aec.h"

#include <Arduino.h>
#include <math.h>
#include <stdio.h>

#include "faces/shared/pm_face_draw.h"
#include "pm_audio_analyzer.h"
#include "pm_display.h"
#include "pm_speaker.h"
#include "pm_voice.h"

static bool s_test_tone = false;
static uint32_t s_started_ms = 0;
static bool s_have_position = false;
static float s_distance_m = 0.f;
static float s_azimuth_deg = 0.f;
static float s_elevation_deg = 0.f;
static uint32_t s_position_ms = 0;

static int pct(float v) {
  if (v < 0.f) {
    v = 0.f;
  } else if (v > 1.f) {
    v = 1.f;
  }
  return static_cast<int>(lrintf(v * 100.f));
}

static void draw_meter(int x, int y, int w, int h, int value_pct, uint16_t fill, uint16_t frame) {
  if (value_pct < 0) {
    value_pct = 0;
  } else if (value_pct > 100) {
    value_pct = 100;
  }
  pm_gfx->drawRoundRect(x, y, w, h, 5, frame);
  const int inner_w = ((w - 4) * value_pct) / 100;
  if (inner_w > 0) {
    pm_gfx->fillRoundRect(x + 2, y + 2, inner_w, h - 4, 4, fill);
  }
}

bool pm_face_settings_aec_tap(void) {
  if (s_test_tone) {
    pm_speaker_tone_stop();
    s_test_tone = false;
    return true;
  }
  pm_voice_abort();
  if (pm_speaker_is_playing()) {
    pm_speaker_abort();
  }
  pm_audio_analyzer_reset();
  (void)pm_audio_analyzer_mic_begin();
  if (!pm_speaker_play_tone_loop_begin(740.f)) {
    s_test_tone = false;
    return false;
  }
  s_test_tone = true;
  s_started_ms = millis();
  return true;
}

void pm_face_settings_aec_leave(void) {
  if (s_test_tone) {
    pm_speaker_tone_stop();
    s_test_tone = false;
  }
  pm_audio_analyzer_mic_end();
}

void pm_face_settings_aec_tick(void) {
  (void)pm_audio_analyzer_mic_begin();
  pm_audio_analyzer_tick();
}

void pm_face_settings_aec_set_position(float distance_m, float azimuth_deg, float elevation_deg) {
  s_distance_m = distance_m;
  s_azimuth_deg = azimuth_deg;
  s_elevation_deg = elevation_deg;
  s_position_ms = millis();
  s_have_position = true;
}

void pm_face_settings_aec_draw(void) {
  const uint16_t c_hi = pm_gfx->color565(215, 225, 235);
  const uint16_t c_dim = pm_gfx->color565(115, 126, 145);
  const uint16_t c_ref = pm_gfx->color565(230, 190, 95);
  const uint16_t c_mic = pm_gfx->color565(110, 215, 185);
  const uint16_t c_err = pm_gfx->color565(120, 155, 235);
  const uint16_t c_bad = pm_gfx->color565(230, 115, 105);

  PmAudioAnalyzerDebug audio = {};
  PmAudioAecDebug aec = {};
  pm_audio_analyzer_debug(&audio);
  pm_audio_analyzer_aec_debug(&aec);

  pm_face_draw_centered_line("AEC", 54, c_hi, 2, 2);
  pm_face_draw_centered_line(s_test_tone ? "tap stops reference tone" : "tap starts reference tone", 94,
                             s_test_tone ? c_ref : c_dim, 1, 1);

  char line[80];
  snprintf(line, sizeof(line), "ref %d%%  out %d%%", pct(aec.ref_rms * 8.f), pct(audio.out_peak));
  pm_face_draw_centered_line(line, 132, c_ref, 1, 1);
  draw_meter(86, 154, 294, 18, pct(aec.ref_rms * 8.f), c_ref, c_dim);

  snprintf(line, sizeof(line), "mic0 %d%%  after %d%%", pct(audio.in_peak[0] * 3.f), pct(aec.err_rms[0] * 4.f));
  pm_face_draw_centered_line(line, 200, c_mic, 1, 1);
  draw_meter(86, 222, 294, 16, pct(audio.in_peak[0] * 3.f), c_mic, c_dim);
  draw_meter(86, 244, 294, 16, pct(aec.err_rms[0] * 4.f), c_err, c_dim);

  snprintf(line, sizeof(line), "mic1 %d%%  after %d%%", pct(audio.in_peak[1] * 3.f), pct(aec.err_rms[1] * 4.f));
  pm_face_draw_centered_line(line, 286, c_mic, 1, 1);
  draw_meter(86, 308, 294, 16, pct(audio.in_peak[1] * 3.f), c_mic, c_dim);
  draw_meter(86, 330, 294, 16, pct(aec.err_rms[1] * 4.f), c_err, c_dim);

  snprintf(line, sizeof(line), "learn %lu / %lu",
           static_cast<unsigned long>(aec.adapt_blocks[0]),
           static_cast<unsigned long>(aec.adapt_blocks[1]));
  pm_face_draw_centered_line(line, 370, (aec.adapt_blocks[0] || aec.adapt_blocks[1]) ? c_hi : c_bad, 1, 1);

  if (s_have_position) {
    const uint32_t age_ms = millis() - s_position_ms;
    snprintf(line, sizeof(line), "uwb %.2fm  az %.0f  el %.0f  %lus",
             static_cast<double>(s_distance_m), static_cast<double>(s_azimuth_deg),
             static_cast<double>(s_elevation_deg), static_cast<unsigned long>(age_ms / 1000u));
    pm_face_draw_centered_line(line, 394, age_ms < 3000u ? c_hi : c_dim, 1, 1);
  } else if (s_test_tone) {
    const uint32_t seconds = (millis() - s_started_ms) / 1000u;
    snprintf(line, sizeof(line), "740 Hz  %lus", static_cast<unsigned long>(seconds));
    pm_face_draw_centered_line(line, 394, c_dim, 1, 1);
  } else {
    pm_face_draw_centered_line("iPhone UWB app supplies pose", 394, c_dim, 1, 1);
  }
}
