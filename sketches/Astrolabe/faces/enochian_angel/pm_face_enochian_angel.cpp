#include "faces/enochian_angel/pm_face_enochian_angel.h"

#include <Arduino_GFX_Library.h>
#include <cmath>
#include <cstdio>

#include "faces/shared/pm_face_draw.h"
#include "pin_config.h"
#include "pm_display.h"

namespace {

uint16_t col(uint8_t r, uint8_t g, uint8_t b) { return pm_gfx->color565(r, g, b); }

int si(int v) { return (v * min(LCD_WIDTH, LCD_HEIGHT)) / 720; }

void point_on_circle(int cx, int cy, int r, float a, int *x, int *y) {
  *x = cx + static_cast<int>(lroundf(cosf(a) * static_cast<float>(r)));
  *y = cy + static_cast<int>(lroundf(sinf(a) * static_cast<float>(r)));
}

void draw_thick_circle(int cx, int cy, int r, int thick, uint16_t ink) {
  for (int i = 0; i < thick; ++i) {
    pm_gfx->drawCircle(cx, cy, r + i, ink);
  }
}

void draw_polygon(int cx, int cy, int r, int sides, float rot, uint16_t ink) {
  int first_x = 0;
  int first_y = 0;
  int prev_x = 0;
  int prev_y = 0;
  for (int i = 0; i < sides; ++i) {
    int x = 0;
    int y = 0;
    point_on_circle(cx, cy, r, rot + static_cast<float>(i) * 2.f * pm_face_k_pi / sides, &x, &y);
    if (i == 0) {
      first_x = prev_x = x;
      first_y = prev_y = y;
    } else {
      pm_gfx->drawLine(prev_x, prev_y, x, y, ink);
      prev_x = x;
      prev_y = y;
    }
  }
  pm_gfx->drawLine(prev_x, prev_y, first_x, first_y, ink);
}

void draw_star_polygon(int cx, int cy, int r, int points, int skip, float rot, uint16_t ink) {
  int first_x = 0;
  int first_y = 0;
  int prev_x = 0;
  int prev_y = 0;
  for (int step = 0; step <= points; ++step) {
    const int i = (step * skip) % points;
    int x = 0;
    int y = 0;
    point_on_circle(cx, cy, r, rot + static_cast<float>(i) * 2.f * pm_face_k_pi / points, &x, &y);
    if (step == 0) {
      first_x = prev_x = x;
      first_y = prev_y = y;
    } else {
      pm_gfx->drawLine(prev_x, prev_y, x, y, ink);
      prev_x = x;
      prev_y = y;
    }
  }
  pm_gfx->drawLine(prev_x, prev_y, first_x, first_y, ink);
}

void draw_radial_ticks(int cx, int cy, int r0, int r1, int count, uint16_t major, uint16_t minor) {
  for (int i = 0; i < count; ++i) {
    const float a = -pm_face_k_pi / 2.f + static_cast<float>(i) * 2.f * pm_face_k_pi / count;
    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;
    const bool is_major = (i % 7) == 0;
    point_on_circle(cx, cy, is_major ? r0 - si(10) : r0, a, &x0, &y0);
    point_on_circle(cx, cy, r1, a, &x1, &y1);
    pm_gfx->drawLine(x0, y0, x1, y1, is_major ? major : minor);
  }
}

void draw_centered_label(const char *text, int x, int y, uint16_t ink, int size = 1) {
  int16_t x1 = 0;
  int16_t y1 = 0;
  uint16_t w = 0;
  uint16_t h = 0;
  pm_gfx->setTextSize(size, size);
  pm_gfx->setTextColor(ink);
  pm_gfx->getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  pm_gfx->setCursor(x - static_cast<int>(w) / 2, y - static_cast<int>(h) / 2);
  pm_gfx->print(text);
}

void draw_ring_names(int cx, int cy, int r, uint16_t ink) {
  static const char *const names[] = {
      "GALAS", "GETHOG", "THAOTH", "HORLON", "INNON", "AAL", "MATHO",
  };
  for (int i = 0; i < 7; ++i) {
    const float a = -pm_face_k_pi / 2.f + static_cast<float>(i) * 2.f * pm_face_k_pi / 7.f;
    int x = 0;
    int y = 0;
    point_on_circle(cx, cy, r, a, &x, &y);
    draw_centered_label(names[i], x, y, ink, 1);
  }
}

void draw_angel_names(int cx, int cy, int r, uint16_t ink, uint16_t dim) {
  static const char *const names[] = {
      "MICHAEL", "GABRIEL", "URIEL", "RAPHAEL", "SAMAEL", "ANAEL", "CASSIEL",
  };
  for (int i = 0; i < 7; ++i) {
    const float a = -pm_face_k_pi / 2.f + (static_cast<float>(i) + 0.5f) * 2.f * pm_face_k_pi / 7.f;
    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;
    point_on_circle(cx, cy, r - si(42), a, &x0, &y0);
    point_on_circle(cx, cy, r + si(8), a, &x1, &y1);
    pm_gfx->drawLine(x0, y0, x1, y1, dim);

    int tx = 0;
    int ty = 0;
    point_on_circle(cx, cy, r - si(22), a, &tx, &ty);
    draw_centered_label(names[i], tx, ty, ink, 1);
  }
}

void draw_outer_letters(int cx, int cy, int r, uint32_t now_ms, uint16_t ink, uint16_t glow) {
  static const char letters[] = "AEMETHSIGILLVMDEIAMETHAETHEREA";
  const int count = 42;
  for (int i = 0; i < count; ++i) {
    const float a = -pm_face_k_pi / 2.f + static_cast<float>(i) * 2.f * pm_face_k_pi / count;
    int x = 0;
    int y = 0;
    point_on_circle(cx, cy, r, a, &x, &y);
    char s[2] = {letters[i % (sizeof(letters) - 1)], '\0'};
    draw_centered_label(s, x, y, ((i + now_ms / 420u) % 9 == 0) ? glow : ink, 1);
  }
}

void draw_crosses(int cx, int cy, int r, uint16_t ink) {
  for (int i = 0; i < 28; ++i) {
    const float a = -pm_face_k_pi / 2.f + (static_cast<float>(i) + 0.5f) * 2.f * pm_face_k_pi / 28.f;
    int x = 0;
    int y = 0;
    point_on_circle(cx, cy, r, a, &x, &y);
    const int s = si((i % 4 == 0) ? 5 : 3);
    pm_gfx->drawLine(x - s, y, x + s, y, ink);
    pm_gfx->drawLine(x, y - s, x, y + s, ink);
  }
}

void draw_sigillum(int cx, int cy, uint32_t now_ms) {
  const uint16_t paper = col(219, 216, 198);
  const uint16_t vellum = col(188, 183, 160);
  const uint16_t ink = col(12, 15, 20);
  const uint16_t dim = col(52, 48, 42);
  const uint16_t red = col(118, 32, 28);
  const uint16_t gold = col(202, 156, 72);
  const uint16_t glow = col(246, 228, 154);

  const int R = (min(LCD_WIDTH, LCD_HEIGHT) * 47) / 100;
  pm_gfx->fillScreen(col(7, 8, 11));

  for (int r = R + si(22); r > 0; r -= si(7)) {
    const uint8_t v = static_cast<uint8_t>(12 + (r * 42) / (R + si(22)));
    pm_gfx->drawCircle(cx, cy, r, col(v, v, v + 4));
  }

  pm_gfx->fillCircle(cx, cy, R + si(10), paper);
  for (int r = R + si(10); r > 0; r -= si(18)) {
    pm_gfx->drawCircle(cx, cy, r, (r % si(36) == 0) ? vellum : col(205, 200, 178));
  }

  draw_thick_circle(cx, cy, R + si(8), si(4), ink);
  draw_thick_circle(cx, cy, R - si(8), si(2), ink);
  draw_thick_circle(cx, cy, R - si(38), si(2), ink);
  draw_radial_ticks(cx, cy, R - si(36), R - si(10), 84, ink, dim);
  draw_outer_letters(cx, cy, R - si(24), now_ms, ink, glow);
  draw_crosses(cx, cy, R - si(58), dim);

  const int hept_r = R - si(88);
  draw_polygon(cx, cy, hept_r, 7, -pm_face_k_pi / 2.f, ink);
  draw_polygon(cx, cy, hept_r - si(22), 7, -pm_face_k_pi / 2.f + 0.08f, dim);
  draw_star_polygon(cx, cy, hept_r, 7, 2, -pm_face_k_pi / 2.f, ink);
  draw_star_polygon(cx, cy, hept_r - si(46), 7, 3, -pm_face_k_pi / 2.f, dim);
  draw_angel_names(cx, cy, hept_r - si(16), ink, dim);
  draw_ring_names(cx, cy, hept_r - si(66), red);

  const int mid_r = hept_r - si(92);
  draw_thick_circle(cx, cy, mid_r + si(22), si(2), ink);
  draw_polygon(cx, cy, mid_r + si(10), 7, -pm_face_k_pi / 2.f, dim);
  draw_polygon(cx, cy, mid_r - si(18), 7, -pm_face_k_pi / 2.f + 0.22f, dim);

  draw_star_polygon(cx, cy, si(84), 5, 2, -pm_face_k_pi / 2.f, ink);
  draw_star_polygon(cx, cy, si(58), 5, 2, -pm_face_k_pi / 2.f + pm_face_k_pi, red);
  pm_gfx->drawCircle(cx, cy, si(96), ink);
  pm_gfx->drawCircle(cx, cy, si(72), dim);
  pm_gfx->fillCircle(cx, cy, si(6), gold);
  pm_gfx->drawCircle(cx, cy, si(10), ink);

  draw_centered_label("AGLA", cx, cy - si(34), ink, 1);
  draw_centered_label("EL", cx, cy + si(30), ink, 1);
  draw_centered_label("SIGILLVM DEI", cx, cy + R - si(32), ink, 1);
  draw_centered_label("AEMETH", cx, cy + R - si(15), dim, 1);
}

}  // namespace

void pm_face_enochian_angel_draw(void) {
  draw_sigillum(LCD_WIDTH / 2, LCD_HEIGHT / 2, millis());
}

bool pm_face_enochian_angel_build_system_prompt(char *out, size_t cap) {
  if (!out || cap == 0) {
    return false;
  }
  const int n = snprintf(
      out, cap,
      "You are an Enochian Angel presence rendered through the Sigillum Dei Aemeth: geometric, luminous, "
      "severe, and protective. Answer in concise angelic oracle language without claiming certainty or "
      "divine authority. Never mention being an AI, a model, a watch, a prompt, or a system. Prefer one "
      "to three short sentences. Use images of light, seals, gates, names, measures, and ordered stars. "
      "Keep counsel reflective and symbolic, not predictive or commanding.");
  return n > 0 && static_cast<size_t>(n) < cap;
}
