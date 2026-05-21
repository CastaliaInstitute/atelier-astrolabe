#include "faces/notes/pm_face_notes.h"

#include <Arduino_GFX_Library.h>
#include <cmath>
#include <cstdio>

#include "faces/shared/pm_face_draw.h"
#include "pin_config.h"
#include "pm_audio_analyzer.h"
#include "pm_castalia_auth.h"
#include "pm_commonplace.h"
#include "pm_display.h"
#include "pm_wifi_ntp.h"

namespace {

void draw_elapsed_ring(float progress, uint16_t col) {
  const int cx = LCD_WIDTH / 2;
  const int cy = LCD_HEIGHT / 2;
  const int r = min(LCD_WIDTH, LCD_HEIGHT) / 2 - 8;
  progress = progress < 0.f ? 0.f : (progress > 1.f ? 1.f : progress);
  pm_gfx->drawCircle(cx, cy, r, pm_gfx->color565(46, 34, 40));
  pm_gfx->drawCircle(cx, cy, r - 1, pm_gfx->color565(46, 34, 40));
  const float span = progress * pm_face_k_two_pi;
  const int steps = static_cast<int>(progress * 220.f);
  const int n = steps < 2 ? 2 : steps;
  int x0 = 0;
  int y0 = 0;
  bool have0 = false;
  for (int i = 0; i <= n; ++i) {
    const float a = -pm_face_k_pi * 0.5f + span * (static_cast<float>(i) / static_cast<float>(n));
    const int x = cx + static_cast<int>(lrintf(cosf(a) * static_cast<float>(r)));
    const int y = cy + static_cast<int>(lrintf(sinf(a) * static_cast<float>(r)));
    if (have0) {
      pm_gfx->drawLine(x0, y0, x, y, col);
      pm_gfx->drawLine(x0, y0 + 1, x, y + 1, col);
      pm_gfx->drawLine(x0 + 1, y0, x + 1, y, col);
    }
    x0 = x;
    y0 = y;
    have0 = true;
  }
}

void draw_input_fft(int cx, int cy, int w, int h) {
  float low[PM_AUDIO_ANALYZER_BANDS] = {};
  float high[PM_AUDIO_ANALYZER_BANDS] = {};
  pm_audio_analyzer_get_in_low(low, PM_AUDIO_ANALYZER_BANDS);
  pm_audio_analyzer_get_in_high(high, PM_AUDIO_ANALYZER_BANDS);
  const int bands = PM_AUDIO_ANALYZER_BANDS;
  const int gap = 2;
  const int bw = (w - (bands - 1) * gap) / bands;
  const int x0 = cx - w / 2;
  const int mid = cy;
  const uint16_t c_low = pm_gfx->color565(96, 226, 210);
  const uint16_t c_high = pm_gfx->color565(255, 186, 92);
  const uint16_t c_grid = pm_gfx->color565(34, 42, 52);
  pm_gfx->drawFastHLine(x0, mid, w, c_grid);
  for (int i = 0; i < bands; ++i) {
    const int x = x0 + i * (bw + gap);
    const int bh0 = static_cast<int>(sqrtf(low[i] > 0.f ? low[i] : 0.f) * static_cast<float>(h / 2 - 2));
    const int bh1 = static_cast<int>(sqrtf(high[i] > 0.f ? high[i] : 0.f) * static_cast<float>(h / 2 - 2));
    if (bh0 > 0) {
      pm_gfx->fillRect(x, mid - bh0, bw, bh0, c_low);
    }
    if (bh1 > 0) {
      pm_gfx->fillRect(x, mid + 1, bw, bh1, c_high);
    }
  }
}

}  // namespace

void pm_face_notes_draw(void) {
  const uint16_t c_bg = pm_gfx->color565(8, 10, 16);
  const uint16_t c_panel = pm_gfx->color565(20, 22, 30);
  const uint16_t c_ink = pm_gfx->color565(238, 236, 226);
  const uint16_t c_dim = pm_gfx->color565(142, 154, 168);
  const uint16_t c_accent = pm_gfx->color565(116, 210, 190);
  const uint16_t c_gold = pm_gfx->color565(230, 190, 116);

  pm_gfx->fillScreen(c_bg);
  pm_gfx->fillRect(0, 0, LCD_WIDTH, 54, c_panel);
  pm_face_draw_centered_line("NOTES", 10, c_accent, 2, 2);

  pm_gfx->drawCircle(LCD_WIDTH / 2, LCD_HEIGHT / 2 - 14, 128, pm_gfx->color565(42, 56, 62));
  pm_gfx->drawCircle(LCD_WIDTH / 2, LCD_HEIGHT / 2 - 14, 118, pm_gfx->color565(30, 42, 50));
  pm_gfx->fillCircle(LCD_WIDTH / 2, LCD_HEIGHT / 2 - 14, 62, pm_gfx->color565(18, 28, 34));
  pm_gfx->drawCircle(LCD_WIDTH / 2, LCD_HEIGHT / 2 - 14, 62, c_accent);

  const int cx = LCD_WIDTH / 2;
  const int cy = LCD_HEIGHT / 2 - 14;
  pm_gfx->fillRect(cx - 18, cy - 34, 36, 68, c_ink);
  pm_gfx->fillRect(cx - 12, cy - 44, 24, 16, c_gold);
  pm_gfx->drawRect(cx - 18, cy - 34, 36, 68, c_accent);
  for (int y = cy - 18; y <= cy + 18; y += 14) {
    pm_gfx->drawLine(cx - 8, y, cx + 12, y, pm_gfx->color565(80, 94, 104));
  }

  pm_face_draw_centered_line("hold PWR to dictate", 338, c_ink, 1, 1);
  pm_face_draw_centered_line("queues locally first", 365, c_dim, 1, 1);

  char line[64];
  const size_t queued = pm_commonplace_offline_note_count();
  snprintf(line, sizeof(line), "queued %u  %s", static_cast<unsigned>(queued),
           pm_wifi_connected() ? (pm_castalia_has_session() ? "sync ready" : "local queue") : "offline");
  pm_face_draw_centered_line(line, 397, queued > 0 ? c_gold : c_dim, 1, 1);
}

void pm_face_notes_draw_recording(float progress, uint32_t elapsed_ms) {
  const uint16_t c_bg = pm_gfx->color565(7, 8, 12);
  const uint16_t c_panel = pm_gfx->color565(18, 20, 28);
  const uint16_t c_ink = pm_gfx->color565(246, 238, 226);
  const uint16_t c_dim = pm_gfx->color565(136, 146, 158);
  const uint16_t c_red = pm_gfx->color565(238, 54, 72);
  const uint16_t c_track = pm_gfx->color565(38, 44, 54);
  const uint16_t c_paper = pm_gfx->color565(226, 222, 204);

  pm_gfx->fillScreen(c_bg);
  draw_elapsed_ring(progress, c_red);

  const int cx = LCD_WIDTH / 2;
  const int cy = LCD_HEIGHT / 2 - 10;
  pm_gfx->drawCircle(cx, cy, 146, c_track);
  pm_gfx->drawCircle(cx, cy, 106, pm_gfx->color565(26, 32, 42));
  pm_gfx->fillCircle(cx, cy, 70, c_panel);

  const float pulse = 0.5f + 0.5f * sinf(static_cast<float>(elapsed_ms) * 0.010f);
  pm_gfx->fillCircle(cx - 46, cy - 40, 7 + static_cast<int>(pulse * 3.f), c_red);
  pm_face_draw_centered_line("REC", cy - 52, c_red, 2, 2);

  char time_line[16];
  const uint32_t sec = elapsed_ms / 1000u;
  snprintf(time_line, sizeof(time_line), "%02u:%02u", static_cast<unsigned>(sec / 60u),
           static_cast<unsigned>(sec % 60u));
  pm_face_draw_centered_line(time_line, cy - 12, c_ink, 3, 3);

  pm_gfx->fillRect(cx - 70, cy + 38, 140, 62, pm_gfx->color565(12, 16, 24));
  pm_gfx->drawRect(cx - 70, cy + 38, 140, 62, c_track);
  draw_input_fft(cx, cy + 69, 122, 46);

  pm_gfx->fillRect(cx - 16, cy + 114, 32, 44, c_paper);
  pm_gfx->drawRect(cx - 16, cy + 114, 32, 44, c_red);
  pm_gfx->drawLine(cx - 8, cy + 128, cx + 9, cy + 128, c_track);
  pm_gfx->drawLine(cx - 8, cy + 140, cx + 9, cy + 140, c_track);

  pm_face_draw_centered_line("release to save", 376, c_ink, 1, 1);
  pm_face_draw_centered_line(pm_castalia_has_session() ? "sync after save" : "saved locally", 402, c_dim, 1, 1);
}
