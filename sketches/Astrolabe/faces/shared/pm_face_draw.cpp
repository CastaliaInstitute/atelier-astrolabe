#include "faces/shared/pm_face_draw.h"
#include <cmath>
#include "pin_config.h"
#include "pm_display.h"
#include "pm_wifi_ntp.h"

uint16_t pm_face_color565_from_hsv(Arduino_GFX *out, float h_deg, float s, float v) {
  h_deg = fmodf(h_deg, 360.0f);
  if (h_deg < 0) {
    h_deg += 360.0f;
  }
  const float c = v * s;
  const float x = c * (1.0f - fabsf(fmodf(h_deg / 60.0f, 2.0f) - 1.0f));
  const float m = v - c;
  float rp = 0, gp = 0, bp = 0;
  if (h_deg < 60.0f) {
    rp = c;
    gp = x;
  } else if (h_deg < 120.0f) {
    rp = x;
    gp = c;
  } else if (h_deg < 180.0f) {
    gp = c;
    bp = x;
  } else if (h_deg < 240.0f) {
    gp = x;
    bp = c;
  } else if (h_deg < 300.0f) {
    rp = x;
    bp = c;
  } else {
    rp = c;
    bp = x;
  }
  const uint8_t r = static_cast<uint8_t>((rp + m) * 255.0f);
  const uint8_t gv = static_cast<uint8_t>((gp + m) * 255.0f);
  const uint8_t b = static_cast<uint8_t>((bp + m) * 255.0f);
  return out->color565(r, gv, b);
}



void pm_face_draw_centered_line(const char *text, int y, uint16_t fg, uint8_t textSizeX, uint8_t textSizeY) {
  pm_gfx->setTextSize(textSizeX, textSizeY);
  int16_t x1, y1;
  uint16_t w, h;
  pm_gfx->getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  const int x = (LCD_WIDTH - static_cast<int>(w)) / 2;
  pm_gfx->setCursor(x, y);
  pm_gfx->setTextColor(fg);
  pm_gfx->print(text);
}



void pm_face_draw_hand_radial(int cx, int cy, float ang, int len, uint16_t col, int half_w) {
  if (len < 1) {
    return;
  }
  const float ux = cosf(ang);
  const float uy = sinf(ang);
  const float px = -uy;
  const float py = ux;
  const int x1 = cx + static_cast<int>(lrintf(ux * static_cast<float>(len)));
  const int y1 = cy + static_cast<int>(lrintf(uy * static_cast<float>(len)));
  for (int w = -half_w; w <= half_w; ++w) {
    const int ox = static_cast<int>(lrintf(px * static_cast<float>(w)));
    const int oy = static_cast<int>(lrintf(py * static_cast<float>(w)));
    pm_gfx->drawLine(cx + ox, cy + oy, x1 + ox, y1 + oy, col);
  }
}



void pm_face_draw_label_at_polar(int rcx, int rcy, int r, float ang, const char *text, uint16_t col) {
  pm_gfx->setTextSize(1, 1);
  pm_gfx->setTextColor(col);
  int16_t x1, y1;
  uint16_t w, h;
  pm_gfx->getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  const int tx = rcx + static_cast<int>(lrintf(cosf(ang) * static_cast<float>(r))) - static_cast<int>(w) / 2;
  const int ty = rcy + static_cast<int>(lrintf(sinf(ang) * static_cast<float>(r))) - static_cast<int>(h) / 2;
  pm_gfx->setCursor(tx, ty);
  pm_gfx->print(text);
}



void pm_face_draw_radial_annulus_slice(int cx, int cy, float ang, int r0, int r1, uint16_t col, int half_w) {
  if (r1 <= r0 || half_w < 0) {
    return;
  }
  const float ux = cosf(ang);
  const float uy = sinf(ang);
  const float px = -uy;
  const float py = ux;
  const int x0 = cx + static_cast<int>(lrintf(ux * static_cast<float>(r0)));
  const int y0 = cy + static_cast<int>(lrintf(uy * static_cast<float>(r0)));
  const int x1 = cx + static_cast<int>(lrintf(ux * static_cast<float>(r1)));
  const int y1 = cy + static_cast<int>(lrintf(uy * static_cast<float>(r1)));
  for (int w = -half_w; w <= half_w; ++w) {
    const int ox = static_cast<int>(lrintf(px * static_cast<float>(w)));
    const int oy = static_cast<int>(lrintf(py * static_cast<float>(w)));
    pm_gfx->drawLine(x0 + ox, y0 + oy, x1 + ox, y1 + oy, col);
  }
}



void pm_face_draw_circumference_rainbow_24h(bool valid) {
  const int cx = LCD_WIDTH / 2;
  const int cy = LCD_HEIGHT / 2;
  const int R = min(LCD_WIDTH, LCD_HEIGHT) / 2;
  /** Inset a few pixels from the physical edge (bezel / mask). */
  const int r_outer = R - 4;
  const int r_inner = r_outer - 5;
  constexpr int k_seg = 288;
  constexpr int k_half_w = 4;

  auto wrap360 = [](float d) {
    d = fmodf(d, 360.0f);
    if (d < 0.f) {
      d += 360.0f;
    }
    return d;
  };

  for (int s = 0; s < k_seg; ++s) {
    const float amid =
        (static_cast<float>(s) + 0.5f) * (pm_face_k_two_pi / static_cast<float>(k_seg)) - pm_face_k_pi * 0.5f;
    float af = amid + pm_face_k_pi * 0.5f;
    af = fmodf(af, pm_face_k_two_pi);
    if (af < 0.f) {
      af += pm_face_k_two_pi;
    }
    float hue_deg;
    if (valid) {
      /** `af` = 0 at top → midnight; same mapping as `sec_of_day * (360/86400)` on the face. */
      const float sec_of_day = af * (86400.f / pm_face_k_two_pi);
      hue_deg = wrap360(sec_of_day * (360.f / 86400.f));
    } else {
      hue_deg = wrap360(af * (360.f / pm_face_k_two_pi) + fmodf(static_cast<float>(millis()) * 0.025f, 360.f));
    }
    const uint16_t col = pm_face_color565_from_hsv(pm_gfx, hue_deg, pm_face_hsv_s, pm_face_hsv_v);
    pm_face_draw_radial_annulus_slice(cx, cy, amid, r_inner, r_outer, col, k_half_w);
  }
}



void pm_face_draw_thinking_progress_ring(float progress) {
  if (progress < 0.f) {
    progress = 0.f;
  }
  if (progress > 1.f) {
    progress = 1.f;
  }
  const int cx = LCD_WIDTH / 2;
  const int cy = LCD_HEIGHT / 2;
  const int R = min(LCD_WIDTH, LCD_HEIGHT) / 2;
  const int r_ring = R - 10;
  const uint16_t c_track = pm_gfx->color565(36, 40, 52);
  const uint16_t c_arc = pm_gfx->color565(200, 215, 255);

  pm_gfx->drawCircle(cx, cy, r_ring, c_track);

  if (progress <= 0.f) {
    return;
  }
  const float a0 = -pm_face_k_pi * 0.5f;
  const float span = pm_face_k_two_pi * progress;
  const int steps = static_cast<int>(lrintf(span * static_cast<float>(r_ring) / 2.f));
  const int n = steps < 24 ? 24 : (steps > 360 ? 360 : steps);
  int px0 = 0;
  int py0 = 0;
  bool have0 = false;
  for (int i = 0; i <= n; ++i) {
    const float a = a0 + span * (static_cast<float>(i) / static_cast<float>(n));
    const int px = cx + static_cast<int>(lrintf(cosf(a) * static_cast<float>(r_ring)));
    const int py = cy + static_cast<int>(lrintf(sinf(a) * static_cast<float>(r_ring)));
    pm_gfx->drawPixel(px, py, c_arc);
    if (have0) {
      pm_gfx->drawLine(px0, py0, px, py, c_arc);
    }
    px0 = px;
    py0 = py;
    have0 = true;
  }
}



void pm_face_draw_voice_waves_overlay(bool outward, uint32_t t_ms) {
  const int cx = LCD_WIDTH / 2;
  const int cy = LCD_HEIGHT / 2;
  const int R = min(LCD_WIDTH, LCD_HEIGHT) / 2 - 18;
  const float phase = fmodf(static_cast<float>(t_ms) * 0.0045f, 1.f);
  constexpr int k_n = 7;
  for (int i = 0; i < k_n; ++i) {
    float t = phase + static_cast<float>(i) / static_cast<float>(k_n);
    t -= floorf(t);
    const float u = outward ? t : (1.f - t);
    const int r = 22 + static_cast<int>(u * static_cast<float>(R - 22));
    const uint8_t b = static_cast<uint8_t>(70 + u * 150.f);
    const uint16_t col = pm_gfx->color565(static_cast<uint8_t>(b * 0.55f), b, static_cast<uint8_t>(160 + u * 70.f));
    pm_gfx->drawCircle(cx, cy, r, col);
    if (r > 3) {
      pm_gfx->drawCircle(cx, cy, r - 2, col);
    }
  }
}



void pm_face_draw_voice_wave_screen(bool outward, uint32_t t_ms, const char *label) {
  pm_gfx->fillScreen(pm_gfx->color565(8, 10, 18));
  if (pm_time_valid()) {
    pm_face_draw_circumference_rainbow_24h(true);
  }
  pm_face_draw_voice_waves_overlay(outward, t_ms);
  if (label && label[0] != '\0') {
    pm_gfx->fillRect(0, 0, LCD_WIDTH, 40, pm_gfx->color565(10, 12, 22));
    pm_face_draw_centered_line(label, 12, pm_gfx->color565(215, 205, 255), 1, 1);
  }
  pm_gfx->flush();
}


