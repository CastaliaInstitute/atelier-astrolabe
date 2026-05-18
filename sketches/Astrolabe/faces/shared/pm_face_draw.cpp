#include "faces/shared/pm_face_draw.h"
#include "faces/shared/pm_circadian_hue.h"
#include <cmath>
#include <cstring>
#include "pin_config.h"
#include "pm_display.h"
#include "pm_wifi_ntp.h"

namespace {

constexpr float k_bottom_arc_span_deg = 90.f;
constexpr float k_bottom_arc_center_deg = 180.f;
constexpr int k_bottom_arc_rim_inset_px = 10;

struct PmFaceBottomArcResolved {
  int cx;
  int cy;
  int r;
  float start_rad;
  float end_rad;
  float arc_len_px;
  uint8_t size_x;
  uint8_t size_y;
  uint16_t color;
};

void pm_face_resolve_bottom_arc_label(const PmFaceBottomArcLabelStyle *style, PmFaceBottomArcResolved *out) {
  const int cx = LCD_WIDTH / 2;
  const int cy = LCD_HEIGHT / 2;
  const int R = min(LCD_WIDTH, LCD_HEIGHT) / 2;
  const int r_rainbow_inner = R - 4 - 5;

  float span_deg = k_bottom_arc_span_deg;
  float center_deg = k_bottom_arc_center_deg;
  int r_px = r_rainbow_inner - k_bottom_arc_rim_inset_px;
  uint8_t size_x = 1;
  uint8_t size_y = 1;
  uint16_t color = pm_gfx->color565(235, 232, 250);

  if (style) {
    if (style->r_px > 0) {
      r_px = style->r_px;
    }
    if (style->arc_span_deg > 0.f) {
      span_deg = style->arc_span_deg;
    }
    if (style->center_deg_clockwise > 0.f) {
      center_deg = style->center_deg_clockwise;
    }
    if (style->text_size_x > 0) {
      size_x = style->text_size_x;
    }
    if (style->text_size_y > 0) {
      size_y = style->text_size_y;
    }
    if (style->color != 0) {
      color = style->color;
    }
  }

  const float start_deg = center_deg - span_deg * 0.5f;
  const float end_deg = center_deg + span_deg * 0.5f;
  out->cx = cx;
  out->cy = cy;
  out->r = r_px;
  out->start_rad = pm_face_deg_to_rad(start_deg);
  out->end_rad = pm_face_deg_to_rad(end_deg);
  out->arc_len_px = static_cast<float>(r_px) * span_deg * (pm_face_k_pi / 180.f);
  out->size_x = size_x;
  out->size_y = size_y;
  out->color = color;
}

uint16_t pm_face_measure_char_width(char ch, uint8_t size_x, uint8_t size_y) {
  char buf[2] = {ch, '\0'};
  pm_gfx->setTextSize(size_x, size_y);
  int16_t x1, y1;
  uint16_t w, h;
  pm_gfx->getTextBounds(buf, 0, 0, &x1, &y1, &w, &h);
  return w;
}

void pm_face_draw_char_on_arc(const PmFaceBottomArcResolved &arc, float ang, char ch) {
  char buf[2] = {ch, '\0'};
  pm_gfx->setTextSize(arc.size_x, arc.size_y);
  pm_gfx->setTextColor(arc.color);
  int16_t x1, y1;
  uint16_t w, h;
  pm_gfx->getTextBounds(buf, 0, 0, &x1, &y1, &w, &h);
  const int tx = arc.cx + static_cast<int>(lrintf(cosf(ang) * static_cast<float>(arc.r))) - static_cast<int>(w) / 2;
  const int ty = arc.cy + static_cast<int>(lrintf(sinf(ang) * static_cast<float>(arc.r))) - static_cast<int>(h) / 2;
  pm_gfx->setCursor(tx, ty);
  pm_gfx->print(buf);
}

void pm_face_draw_bottom_arc_label_at_offset(const char *text, const PmFaceBottomArcLabelStyle *style,
                                             float along_px) {
  if (!text || text[0] == '\0') {
    return;
  }

  PmFaceBottomArcResolved arc = {};
  pm_face_resolve_bottom_arc_label(style, &arc);
  if (arc.arc_len_px < 4.f || arc.r < 8) {
    return;
  }

  const size_t n = strlen(text);
  if (n == 0 || n > 96) {
    return;
  }

  uint16_t widths[96];
  float total_w = 0.f;
  for (size_t i = 0; i < n; ++i) {
    widths[i] = pm_face_measure_char_width(text[i], arc.size_x, arc.size_y);
    total_w += static_cast<float>(widths[i]);
  }

  float x = -along_px;
  for (size_t pass = 0; pass < 2 && x < arc.arc_len_px + total_w; ++pass) {
    float cursor = x;
    for (size_t i = 0; i < n; ++i) {
      const float cx_along = cursor + static_cast<float>(widths[i]) * 0.5f;
      if (cx_along >= -static_cast<float>(widths[i]) && cx_along <= arc.arc_len_px + static_cast<float>(widths[i])) {
        const float t = cx_along / arc.arc_len_px;
        const float ang = arc.start_rad + t * (arc.end_rad - arc.start_rad);
        if (ang >= arc.start_rad - 0.05f && ang <= arc.end_rad + 0.05f) {
          pm_face_draw_char_on_arc(arc, ang, text[i]);
        }
      }
      cursor += static_cast<float>(widths[i]);
    }
    x += total_w + static_cast<float>(widths[0]);
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
  const int cx = LCD_WIDTH / 2;
  const int cy = LCD_HEIGHT / 2;
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

void pm_face_draw_bottom_arc_label_static(const char *text, const PmFaceBottomArcLabelStyle *style) {
  if (!text || text[0] == '\0') {
    return;
  }

  PmFaceBottomArcResolved arc = {};
  pm_face_resolve_bottom_arc_label(style, &arc);
  if (arc.arc_len_px < 4.f || arc.r < 8) {
    return;
  }

  const size_t n = strlen(text);
  if (n == 0 || n > 96) {
    return;
  }

  uint16_t widths[96];
  float total_w = 0.f;
  for (size_t i = 0; i < n; ++i) {
    widths[i] = pm_face_measure_char_width(text[i], arc.size_x, arc.size_y);
    total_w += static_cast<float>(widths[i]);
  }

  float cursor = (arc.arc_len_px - total_w) * 0.5f;
  if (cursor < 0.f) {
    cursor = 0.f;
  }
  for (size_t i = 0; i < n; ++i) {
    const float cx_along = cursor + static_cast<float>(widths[i]) * 0.5f;
    const float t = cx_along / arc.arc_len_px;
    const float ang = arc.start_rad + t * (arc.end_rad - arc.start_rad);
    pm_face_draw_char_on_arc(arc, ang, text[i]);
    cursor += static_cast<float>(widths[i]);
  }
}

void pm_face_draw_bottom_arc_label_scroll(const char *text, uint32_t t_ms, float scroll_px_per_sec,
                                          const PmFaceBottomArcLabelStyle *style) {
  if (!text || text[0] == '\0') {
    return;
  }

  PmFaceBottomArcResolved arc = {};
  pm_face_resolve_bottom_arc_label(style, &arc);
  if (arc.arc_len_px < 4.f || arc.r < 8) {
    return;
  }

  const size_t n = strlen(text);
  if (n == 0 || n > 96) {
    return;
  }

  float total_w = 0.f;
  for (size_t i = 0; i < n; ++i) {
    total_w += static_cast<float>(pm_face_measure_char_width(text[i], arc.size_x, arc.size_y));
  }

  const float speed = scroll_px_per_sec > 0.f ? scroll_px_per_sec : 28.f;
  const float loop = total_w + static_cast<float>(pm_face_measure_char_width(' ', arc.size_x, arc.size_y)) * 2.f;
  const float along = fmodf(static_cast<float>(t_ms) * 0.001f * speed, loop);
  pm_face_draw_bottom_arc_label_at_offset(text, style, along);
}


