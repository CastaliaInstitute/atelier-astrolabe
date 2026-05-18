#include "faces/shared/pm_face_draw.h"
#include "faces/shared/pm_circadian_hue.h"
#include <cmath>
#include "pin_config.h"
#include "pm_config.h"
#include "pm_display.h"
#include "pm_home_gem_pulse.h"
#include "pm_wifi_ntp.h"

namespace {

uint16_t blend565(uint16_t bg, uint16_t fg, float alpha) {
  if (alpha <= 0.f) {
    return bg;
  }
  if (alpha >= 1.f) {
    return fg;
  }
  const uint8_t br = static_cast<uint8_t>(((bg >> 11) & 0x1F) * 255 / 31);
  const uint8_t bg_g = static_cast<uint8_t>(((bg >> 5) & 0x3F) * 255 / 63);
  const uint8_t bb = static_cast<uint8_t>((bg & 0x1F) * 255 / 31);
  const uint8_t fr = static_cast<uint8_t>(((fg >> 11) & 0x1F) * 255 / 31);
  const uint8_t fg_g = static_cast<uint8_t>(((fg >> 5) & 0x3F) * 255 / 63);
  const uint8_t fb = static_cast<uint8_t>((fg & 0x1F) * 255 / 31);
  const float a = alpha;
  const float ia = 1.f - a;
  return pm_gfx->color565(static_cast<uint8_t>(br * ia + fr * a), static_cast<uint8_t>(bg_g * ia + fg_g * a),
                          static_cast<uint8_t>(bb * ia + fb * a));
}

float smoothstep01(float t) {
  if (t <= 0.f) {
    return 0.f;
  }
  if (t >= 1.f) {
    return 1.f;
  }
  return t * t * (3.f - 2.f * t);
}

void hsv_to_rgb255(float h_deg, float s, float v, float &r, float &g, float &b) {
  h_deg = fmodf(h_deg, 360.0f);
  if (h_deg < 0.f) {
    h_deg += 360.f;
  }
  const float c = v * s;
  const float x = c * (1.f - fabsf(fmodf(h_deg / 60.f, 2.f) - 1.f));
  const float m = v - c;
  float rp = 0.f, gp = 0.f, bp = 0.f;
  if (h_deg < 60.f) {
    rp = c;
    gp = x;
  } else if (h_deg < 120.f) {
    rp = x;
    gp = c;
  } else if (h_deg < 180.f) {
    gp = c;
    bp = x;
  } else if (h_deg < 240.f) {
    gp = x;
    bp = c;
  } else if (h_deg < 300.f) {
    rp = x;
    bp = c;
  } else {
    rp = c;
    bp = x;
  }
  r = (rp + m) * 255.f;
  g = (gp + m) * 255.f;
  b = (bp + m) * 255.f;
}

uint32_t frost_hash(int x, int y) {
  uint32_t h = static_cast<uint32_t>(x * 374761393 + y * 668265263);
  h = (h ^ (h >> 13)) * 1274126177u;
  return h ^ (h >> 16);
}

/** Linear in radius (no center hot-spot). */
float gem_radial_glow(float t) { return 1.f - t; }

uint16_t rgb255_ordered_dither_565(int x, int y, float r, float g, float b) {
  static const uint8_t k_bayer8[8][8] = {
      {0, 48, 12, 60, 3, 51, 15, 63},  {32, 16, 44, 28, 35, 19, 47, 31}, {8, 56, 4, 52, 11, 59, 7, 55},
      {40, 24, 36, 20, 43, 27, 39, 23}, {2, 50, 14, 62, 1, 49, 13, 61},  {34, 18, 46, 30, 33, 17, 45, 29},
      {10, 58, 6, 54, 9, 57, 5, 53},   {42, 26, 38, 22, 41, 25, 37, 21}};
  const float th = (static_cast<float>(k_bayer8[y & 7][x & 7]) + 0.5f) / 64.f;
  auto q = [&](float c) {
    if (c < 0.f) {
      c = 0.f;
    } else if (c > 255.f) {
      c = 255.f;
    }
    int lo = static_cast<int>(c);
    const float frac = c - static_cast<float>(lo);
    if (frac > th) {
      ++lo;
    }
    if (lo > 255) {
      lo = 255;
    }
    return static_cast<uint8_t>(lo);
  };
  return pm_gfx->color565(q(r), q(g), q(b));
}

uint16_t gem_color_at_radius(int x, int y, float t, float hue_deg, float pulse_brightness) {
  constexpr float k_peak_v = 0.40f;
  const float glow = gem_radial_glow(t);
  float v = pm_face_hsv_v + (k_peak_v - pm_face_hsv_v) * glow;
  v += (static_cast<float>((frost_hash(x, y) >> 8) & 255u) - 127.5f) / 6144.f;
  v *= pulse_brightness;
  if (v > 1.f) {
    v = 1.f;
  }
  if (v < 0.f) {
    v = 0.f;
  }
  const float sat = pm_face_hsv_s * (0.82f + 0.18f * glow);
  float r, g, b;
  hsv_to_rgb255(hue_deg, sat, v, r, g, b);
  if (t > 0.58f) {
    const float edge = smoothstep01((t - 0.58f) / 0.42f);
    float vr, vg, vb;
    hsv_to_rgb255(hue_deg, pm_face_hsv_s * 0.48f, 0.02f, vr, vg, vb);
    const float iv = 1.f - edge;
    r = r * iv + vr * edge;
    g = g * iv + vg * edge;
    b = b * iv + vb * edge;
  }
  return rgb255_ordered_dither_565(x, y, r, g, b);
}

void gem_fill_radial_dithered(int cx, int cy, int r_max, float hue_deg, float pulse_b) {
  const int r_max2 = r_max * r_max;
  const float inv_r_max = 1.f / static_cast<float>(r_max);
  const int y0 = cy - r_max;
  const int y1 = cy + r_max;
  for (int y = y0; y <= y1; ++y) {
    const int dy = y - cy;
    const int dy2 = dy * dy;
    if (dy2 > r_max2) {
      continue;
    }
    const int half = static_cast<int>(lrintf(sqrtf(static_cast<float>(r_max2 - dy2))));
    int xa = cx - half;
    int xb = cx + half;
    if (xa < 0) {
      xa = 0;
    }
    if (xb >= LCD_WIDTH) {
      xb = LCD_WIDTH - 1;
    }
    for (int x = xa; x <= xb; ++x) {
      const int dx = x - cx;
      const int d2 = dx * dx + dy2;
      if (d2 > r_max2) {
        continue;
      }
      const float t = sqrtf(static_cast<float>(d2)) * inv_r_max;
      pm_gfx->drawPixel(x, y, gem_color_at_radius(x, y, t, hue_deg, pulse_b));
    }
  }
}

}  // namespace

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



float pm_face_deg_to_rad(float deg_clockwise_from_top) {
  return (deg_clockwise_from_top - 90.f) * pm_face_k_pi / 180.f;
}

void pm_face_draw_annular_wedge(int cx, int cy, int r_inner, int r_outer, float start_deg, float end_deg,
                                uint16_t fill_col) {
  if (r_outer <= r_inner || end_deg <= start_deg) {
    return;
  }
  const float span = end_deg - start_deg;
  const int steps = static_cast<int>(lrintf(span * 0.35f));
  const int n = steps < 4 ? 4 : (steps > 48 ? 48 : steps);
  for (int i = 0; i < n; ++i) {
    const float t0 = start_deg + span * (static_cast<float>(i) / static_cast<float>(n));
    const float t1 = start_deg + span * (static_cast<float>(i + 1) / static_cast<float>(n));
    const float a0 = pm_face_deg_to_rad(t0);
    const float a1 = pm_face_deg_to_rad(t1);
    const int x0i = cx + static_cast<int>(lrintf(cosf(a0) * static_cast<float>(r_inner)));
    const int y0i = cy + static_cast<int>(lrintf(sinf(a0) * static_cast<float>(r_inner)));
    const int x0o = cx + static_cast<int>(lrintf(cosf(a0) * static_cast<float>(r_outer)));
    const int y0o = cy + static_cast<int>(lrintf(sinf(a0) * static_cast<float>(r_outer)));
    const int x1o = cx + static_cast<int>(lrintf(cosf(a1) * static_cast<float>(r_outer)));
    const int y1o = cy + static_cast<int>(lrintf(sinf(a1) * static_cast<float>(r_outer)));
    const int x1i = cx + static_cast<int>(lrintf(cosf(a1) * static_cast<float>(r_inner)));
    const int y1i = cy + static_cast<int>(lrintf(sinf(a1) * static_cast<float>(r_inner)));
    pm_gfx->fillTriangle(x0i, y0i, x0o, y0o, x1o, y1o, fill_col);
    pm_gfx->fillTriangle(x0i, y0i, x1o, y1o, x1i, y1i, fill_col);
  }
}

void pm_face_draw_daywheel_hue_ring_12h(int64_t now_unix, int r_inner, int r_outer) {
  const int cx = pm_face_lcd_cx;
  const int cy = pm_face_lcd_cy;
  constexpr int64_t k_window = 12 * 3600;
  constexpr int k_seg = 144;
  for (int s = 0; s < k_seg; ++s) {
    const float deg0 = static_cast<float>(s) * (360.f / static_cast<float>(k_seg));
    const float deg1 = static_cast<float>(s + 1) * (360.f / static_cast<float>(k_seg));
    const int64_t t_mid = now_unix + static_cast<int64_t>((deg0 + deg1) * 0.5f * static_cast<float>(k_window) / 360.f);
    const uint16_t col = pm_circadian_color565_at_unix(static_cast<time_t>(t_mid));
    pm_face_draw_annular_wedge(cx, cy, r_inner, r_outer, deg0, deg1, col);
  }
}

void pm_face_draw_now_bead(int cx, int cy, int r, uint16_t col) {
  const int bx = cx;
  const int by = cy - r;
  pm_gfx->fillCircle(bx, by, 5, col);
  pm_gfx->drawCircle(bx, by, 6, pm_gfx->color565(255, 255, 255));
}

void pm_face_draw_circumference_rainbow_24h(bool valid) {
  const int cx = pm_face_lcd_cx;
  const int cy = pm_face_lcd_cy;
  const int R = min(LCD_WIDTH, LCD_HEIGHT) / 2;
  const int r_outer = R - 9;
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
  const int cx = pm_face_lcd_cx;
  const int cy = pm_face_lcd_cy;
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
  const int cx = pm_face_lcd_cx;
  const int cy = pm_face_lcd_cy;
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

uint16_t pm_face_draw_home_gem_glow(float hue_deg_24h) {
  const int cx = pm_face_lcd_cx;
  const int cy = pm_face_lcd_cy;
  const int R = min(LCD_WIDTH, LCD_HEIGHT) / 2;
  /** Match rainbow inner radius (r_outer − 5) so gem and rim share one center. */
  const int r_max = R - 14;

  const float pulse_b = pm_home_gem_pulse_brightness(millis());

  const uint16_t edge = gem_color_at_radius(cx, cy, 1.f, hue_deg_24h, pulse_b);
  pm_gfx->fillScreen(edge);
  gem_fill_radial_dithered(cx, cy, r_max, hue_deg_24h, pulse_b);

  return gem_color_at_radius(cx, cy, 0.35f, hue_deg_24h, pulse_b);
}

void pm_face_draw_home_gem_breath_only(float hue_deg_24h) {
  const int cx = pm_face_lcd_cx;
  const int cy = pm_face_lcd_cy;
  const int R = min(LCD_WIDTH, LCD_HEIGHT) / 2;
  const int r_max = R - 14;
  const float pulse_b = pm_home_gem_pulse_brightness(millis());
  gem_fill_radial_dithered(cx, cy, r_max, hue_deg_24h, pulse_b);
}

