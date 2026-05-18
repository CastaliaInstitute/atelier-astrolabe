#include "faces/spectrum/pm_face_spectrum.h"

#include <cmath>

#include "faces/shared/pm_face_draw.h"
#include "pin_config.h"
#include "pm_audio_analyzer.h"
#include "pm_display.h"

static bool s_active = false;

void pm_face_spectrum_on_enter(void) {
  pm_audio_analyzer_reset();
  (void)pm_audio_analyzer_mic_begin();
  s_active = true;
}

void pm_face_spectrum_on_leave(void) {
  if (!s_active) {
    return;
  }
  pm_audio_analyzer_mic_end();
  s_active = false;
}

void pm_face_spectrum_tick(void) {
  if (!s_active) {
    return;
  }
  pm_audio_analyzer_tick();
}

void pm_face_spectrum_draw(uint16_t bg) {
  (void)bg;

  const int cx = LCD_WIDTH / 2;
  const int cy = LCD_HEIGHT / 2;

  float bins_in[PM_AUDIO_ANALYZER_BANDS];
  float bins_out[PM_AUDIO_ANALYZER_BANDS];
  pm_audio_analyzer_get_in(bins_in, PM_AUDIO_ANALYZER_BANDS);
  pm_audio_analyzer_get_out(bins_out, PM_AUDIO_ANALYZER_BANDS);

  pm_gfx->fillScreen(RGB565_BLACK);

  constexpr int k_bands = PM_AUDIO_ANALYZER_BANDS;
  constexpr float k_step = pm_face_k_two_pi / static_cast<float>(k_bands);
  constexpr int k_r_in0 = 52;
  constexpr int k_r_in1 = 128;
  constexpr int k_r_out0 = 142;
  constexpr int k_r_out1 = 218;

  for (int b = 0; b < k_bands; ++b) {
    const float ang = -pm_face_k_pi / 2.f + static_cast<float>(b) * k_step;
    const float vi = bins_in[b];
    const int ri1 = k_r_in0 + static_cast<int>(vi * static_cast<float>(k_r_in1 - k_r_in0));
    const uint16_t cin = pm_face_color565_from_hsv(pm_gfx, 165.f + vi * 55.f, 0.85f, 0.12f + vi * 0.55f);
    pm_face_draw_radial_annulus_slice(cx, cy, ang, k_r_in0, ri1 > k_r_in0 ? ri1 : k_r_in0 + 1, cin, 2);

    const float vo = bins_out[b];
    const int ro1 = k_r_out0 + static_cast<int>(vo * static_cast<float>(k_r_out1 - k_r_out0));
    const uint16_t cout = pm_face_color565_from_hsv(pm_gfx, 285.f + vo * 45.f, 0.9f, 0.1f + vo * 0.6f);
    pm_face_draw_radial_annulus_slice(cx, cy, ang, k_r_out0, ro1 > k_r_out0 ? ro1 : k_r_out0 + 1, cout, 2);
  }

  pm_face_draw_centered_line("IN", cy - 12, pm_gfx->color565(80, 200, 220), 1, 1);
  pm_face_draw_centered_line("OUT", cy + 22, pm_gfx->color565(220, 120, 200), 1, 1);
}
