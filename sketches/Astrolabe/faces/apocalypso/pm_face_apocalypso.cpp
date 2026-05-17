#include "faces/apocalypso/pm_face_apocalypso.h"
#include "faces/shared/pm_face_draw.h"
#include <cmath>
#include <cstdio>
#include "pin_config.h"
#include "pm_display.h"

void pm_face_apocalypso_draw(const struct tm *tm, bool valid) {
  (void)tm;
  (void)valid;

  const uint16_t c_ring = pm_gfx->color565(55, 65, 82);
  const uint16_t c_spoke = pm_gfx->color565(72, 84, 102);
  const uint16_t c_fill = pm_gfx->color565(55, 140, 215);
  const uint16_t c_outline = RGB565_WHITE;
  const uint16_t c_pct = pm_gfx->color565(140, 148, 158);
  const uint16_t c_white = RGB565_WHITE;

  static const char *const k_lab[12] = {
      "Biblical", "Nuclear", "Bio",       "AI",      "Cyber",    "Infra",
      "Market",   "State",   "Epistemic", "Climate", "Biosphere", "Solar",
  };
  static const uint16_t k_col[12] = {
      pm_gfx->color565(227, 179, 65),  pm_gfx->color565(255, 123, 114), pm_gfx->color565(86, 211, 100),
      pm_gfx->color565(121, 192, 255), pm_gfx->color565(188, 160, 220), pm_gfx->color565(240, 136, 62),
      pm_gfx->color565(227, 200, 80),  pm_gfx->color565(255, 171, 145), pm_gfx->color565(210, 168, 255),
      pm_gfx->color565(86, 212, 220),  pm_gfx->color565(63, 185, 80),   pm_gfx->color565(242, 204, 96),
  };

  /** Demo profile (0..1 of outer ring); Climate peak, AI & Biblical elevated. */
  static const float k_risk[12] = {
      0.28f, 0.10f, 0.15f, 0.28f, 0.12f, 0.14f, 0.08f, 0.10f, 0.12f, 0.45f, 0.18f, 0.12f,
  };

  const int rcx = LCD_WIDTH / 2;
  const int rcy = LCD_HEIGHT / 2;
  const int rmax = 120;
  constexpr int k_axes = 12;

  char ttop[8];
  if (valid) {
    snprintf(ttop, sizeof(ttop), "%02d:%02d", tm->tm_hour, tm->tm_min);
  } else {
    snprintf(ttop, sizeof(ttop), "%s", "--:--");
  }
  const uint16_t c_green = pm_gfx->color565(0x56, 0xd3, 0x64);
  pm_face_draw_centered_line(ttop, 66, c_green, 1, 1);
  pm_face_draw_centered_line("RISK PROFILE", 86, c_pct, 1, 1);

  for (int ring = 1; ring <= 5; ++ring) {
    const int rr = (rmax * ring) / 5;
    pm_gfx->drawCircle(rcx, rcy, rr, c_ring);
  }

  for (int i = 0; i < k_axes; ++i) {
    const float ang = i * (pm_face_k_two_pi / static_cast<float>(k_axes)) - pm_face_k_pi * 0.5f;
    const int xe = rcx + static_cast<int>(lrintf(cosf(ang) * static_cast<float>(rmax)));
    const int ye = rcy + static_cast<int>(lrintf(sinf(ang) * static_cast<float>(rmax)));
    pm_gfx->drawLine(rcx, rcy, xe, ye, c_spoke);
  }

  int vx[12];
  int vy[12];
  for (int i = 0; i < k_axes; ++i) {
    const float ang = i * (pm_face_k_two_pi / static_cast<float>(k_axes)) - pm_face_k_pi * 0.5f;
    const int ri = static_cast<int>(lrintf(static_cast<float>(rmax) * k_risk[i]));
    vx[i] = rcx + static_cast<int>(lrintf(cosf(ang) * static_cast<float>(ri)));
    vy[i] = rcy + static_cast<int>(lrintf(sinf(ang) * static_cast<float>(ri)));
  }

  for (int i = 0; i < k_axes; ++i) {
    const int j = (i + 1) % k_axes;
    pm_gfx->fillTriangle(rcx, rcy, vx[i], vy[i], vx[j], vy[j], c_fill);
  }
  for (int i = 0; i < k_axes; ++i) {
    const int j = (i + 1) % k_axes;
    pm_gfx->drawLine(vx[i], vy[i], vx[j], vy[j], c_outline);
  }

  pm_gfx->drawLine(rcx, rcy, rcx, rcy - rmax, c_white);

  pm_gfx->setTextSize(1, 1);
  pm_gfx->setTextColor(c_pct);
  const char *pct[] = {"100%", "75%", "50%", "25%", "0%"};
  for (int p = 0; p < 5; ++p) {
    const int step = (rmax * (5 - p)) / 5;
    int16_t x1, y1;
    uint16_t w, h;
    pm_gfx->getTextBounds(pct[p], 0, 0, &x1, &y1, &w, &h);
    pm_gfx->setCursor(rcx - 36 - static_cast<int>(w), rcy - step - static_cast<int>(h) / 2);
    pm_gfx->print(pct[p]);
  }

  const int r_lab = rmax + 14;
  for (int i = 0; i < k_axes; ++i) {
    const float ang = i * (pm_face_k_two_pi / static_cast<float>(k_axes)) - pm_face_k_pi * 0.5f;
    pm_face_draw_label_at_polar(rcx, rcy, r_lab, ang, k_lab[i], k_col[i]);
  }

  const float ang_imp = -pm_face_k_pi * 0.5f - (pm_face_k_two_pi / static_cast<float>(k_axes)) * 0.5f;
  pm_face_draw_label_at_polar(rcx, rcy, r_lab + 22, ang_imp, "Impact", c_pct);
}


