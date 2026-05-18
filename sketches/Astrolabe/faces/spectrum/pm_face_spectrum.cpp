#include "faces/spectrum/pm_face_spectrum.h"

#include "faces/shared/pm_face_draw.h"
#include "pin_config.h"
#include "pm_audio_analyzer.h"
#include "pm_audio_route.h"
#include "pm_display.h"

static bool s_active = false;

/** Left top / left bottom / right center panels (466×466 round). */
static constexpr int k_pad = 28;
static constexpr int k_left_x = k_pad;
static constexpr int k_left_w = 188;
static constexpr int k_top_y = 36;
static constexpr int k_top_h = 175;
static constexpr int k_bot_y = 255;
static constexpr int k_bot_h = 175;
static constexpr int k_right_x = 258;
static constexpr int k_right_w = 180;
static constexpr int k_right_y = 128;
static constexpr int k_right_h = 210;

static void draw_panel_label(const char *text, int x, int y, int w, uint16_t fg) {
  pm_gfx->setTextSize(1, 1);
  int16_t x1, y1;
  uint16_t tw, th;
  pm_gfx->getTextBounds(text, 0, 0, &x1, &y1, &tw, &th);
  pm_gfx->setCursor(x + (w - static_cast<int>(tw)) / 2, y);
  pm_gfx->setTextColor(fg);
  pm_gfx->print(text);
}

static void draw_bar_panel(int x, int y, int w, int h, const float *bands, int n_bands, float hue,
                           uint16_t grid_col) {
  if (!bands || n_bands <= 0 || w < 8 || h < 8) {
    return;
  }
  pm_gfx->drawRect(x, y, w, h, grid_col);
  const int bar_w = (w - 4) / n_bands;
  if (bar_w < 2) {
    return;
  }
  const int base_y = y + h - 2;
  for (int b = 0; b < n_bands; ++b) {
    const float v = bands[b];
    if (v < 0.02f) {
      continue;
    }
    int bh = static_cast<int>(v * static_cast<float>(h - 6));
    if (bh < 2) {
      bh = 2;
    }
    const int bx = x + 2 + b * bar_w;
    const int by = base_y - bh;
    const uint16_t col = pm_face_color565_from_hsv(pm_gfx, hue + static_cast<float>(b) * 2.2f, 0.88f,
                                                   0.15f + v * 0.65f);
    pm_gfx->fillRect(bx, by, bar_w - 1, bh, col);
  }
}

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

  float low[PM_AUDIO_ANALYZER_BANDS];
  float high[PM_AUDIO_ANALYZER_BANDS];
  float out[PM_AUDIO_ANALYZER_BANDS];
  pm_audio_analyzer_get_in_low(low, PM_AUDIO_ANALYZER_BANDS);
  pm_audio_analyzer_get_in_high(high, PM_AUDIO_ANALYZER_BANDS);
  pm_audio_analyzer_get_out(out, PM_AUDIO_ANALYZER_BANDS);

  pm_gfx->fillScreen(RGB565_BLACK);
  const uint16_t grid = pm_gfx->color565(40, 44, 52);

  char route_lbl[24];
  pm_audio_route_label(route_lbl, sizeof(route_lbl));
  draw_panel_label(route_lbl, k_right_x, 8, k_right_w, pm_gfx->color565(160, 170, 190));

  draw_panel_label("MIC 1", k_left_x, k_top_y - 14, k_left_w, pm_gfx->color565(70, 190, 210));
  draw_panel_label("MIC 2", k_left_x, k_bot_y - 14, k_left_w, pm_gfx->color565(90, 210, 200));
  draw_panel_label("OUT", k_right_x, k_right_y + k_right_h + 6, k_right_w, pm_gfx->color565(220, 110, 200));

  draw_bar_panel(k_left_x, k_top_y, k_left_w, k_top_h, low, PM_AUDIO_ANALYZER_BANDS, 155.f, grid);
  draw_bar_panel(k_left_x, k_bot_y, k_left_w, k_bot_h, high, PM_AUDIO_ANALYZER_BANDS, 185.f, grid);
  draw_bar_panel(k_right_x, k_right_y, k_right_w, k_right_h, out, PM_AUDIO_ANALYZER_BANDS, 285.f, grid);
}
