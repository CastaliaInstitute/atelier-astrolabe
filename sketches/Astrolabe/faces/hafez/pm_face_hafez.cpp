#include "faces/hafez/pm_face_hafez.h"

#include <cmath>
#include <ctime>
#include <cstring>

#include "faces/shared/pm_face_draw.h"
#include "pin_config.h"
#include "pm_display.h"
#include "pm_wifi_ntp.h"

PmHafezStatus g_hafez_ui = {};
bool s_hafez_have_data = false;

namespace {

uint32_t lcg_next(uint32_t *state) {
  *state = (*state * 1664525u) + 1013904223u;
  return *state;
}

float frand(uint32_t *state) {
  const uint32_t v = lcg_next(state) & 0x00FFFFFFu;
  return static_cast<float>(v) / static_cast<float>(0x01000000u);
}

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
  const float ia = 1.f - alpha;
  return pm_gfx->color565(static_cast<uint8_t>(br * ia + fr * alpha),
                          static_cast<uint8_t>(bg_g * ia + fg_g * alpha),
                          static_cast<uint8_t>(bb * ia + fb * alpha));
}

void draw_generated_art(uint32_t seed, const char *palette_hint) {
  if (seed == 0) {
    seed = static_cast<uint32_t>(time(nullptr));
  }
  uint32_t rng = seed ^ 0x9e3779b9u;
  float base_h = static_cast<float>(seed % 360u);
  if (palette_hint && palette_hint[0]) {
    base_h = fmodf(base_h + static_cast<float>(strlen(palette_hint) * 7u), 360.f);
  }
  const uint16_t c_base = pm_face_color565_from_hsv(pm_gfx, base_h, 0.55f, 0.22f);
  const uint16_t c_fill_a = pm_face_color565_from_hsv(pm_gfx, fmodf(base_h + 32.f, 360.f), 0.62f, 0.52f);
  const uint16_t c_fill_b = pm_face_color565_from_hsv(pm_gfx, fmodf(base_h + 210.f, 360.f), 0.58f, 0.47f);
  const uint16_t c_line = pm_face_color565_from_hsv(pm_gfx, fmodf(base_h + 95.f, 360.f), 0.44f, 0.72f);

  pm_gfx->fillScreen(c_base);

  const int cx = LCD_WIDTH / 2;
  const int cy = LCD_HEIGHT / 2;
  for (int i = 0; i < 22; ++i) {
    const float t = frand(&rng);
    const float u = frand(&rng);
    const int x = static_cast<int>(t * static_cast<float>(LCD_WIDTH));
    const int y = static_cast<int>(u * static_cast<float>(LCD_HEIGHT));
    const int r = 18 + static_cast<int>(frand(&rng) * 96.f);
    const bool use_a = (lcg_next(&rng) & 1u) == 0;
    const float a = 0.10f + frand(&rng) * 0.26f;
    const uint16_t col = blend565(c_base, use_a ? c_fill_a : c_fill_b, a);
    pm_gfx->fillCircle(x, y, r, col);
  }

  for (int i = 0; i < 11; ++i) {
    const int r0 = 52 + static_cast<int>(frand(&rng) * 104.f);
    const int r1 = r0 + 10 + static_cast<int>(frand(&rng) * 46.f);
    float a0 = frand(&rng) * 360.f;
    float a1 = a0 + 20.f + frand(&rng) * 100.f;
    if (a1 > 360.f) {
      a1 -= 360.f;
    }
    if (a1 <= a0) {
      const float t_swap = a0;
      a0 = a1;
      a1 = t_swap;
    }
    const uint16_t col = blend565(c_base, c_fill_b, 0.28f + frand(&rng) * 0.30f);
    pm_face_draw_annular_wedge(cx, cy, r0, r1, a0, a1, col);
  }

  for (int i = 0; i < 26; ++i) {
    const float ang = frand(&rng) * 6.2831853f;
    const int rr = 50 + static_cast<int>(frand(&rng) * 190.f);
    const int x0 = cx + static_cast<int>(cosf(ang) * rr);
    const int y0 = cy + static_cast<int>(sinf(ang) * rr);
    const int x1 = cx + static_cast<int>(cosf(ang + 0.36f) * (rr - 26));
    const int y1 = cy + static_cast<int>(sinf(ang + 0.36f) * (rr - 26));
    pm_gfx->drawLine(x0, y0, x1, y1, c_line);
  }
}

int wrap_quote_lines(const char *text, char lines[][70], int max_lines, int max_cols) {
  if (!text || !text[0] || max_lines <= 0 || max_cols <= 8) {
    return 0;
  }
  int line_idx = 0;
  int col = 0;
  bool last_was_space = false;
  memset(lines, 0, static_cast<size_t>(max_lines) * 70u);
  for (const char *p = text; *p && line_idx < max_lines; ++p) {
    char c = *p;
    if (c == '\n' || c == '\r' || c == '\t') {
      c = ' ';
    }
    if (c == ' ') {
      if (col == 0 || last_was_space) {
        continue;
      }
      last_was_space = true;
    } else {
      last_was_space = false;
    }

    if (col >= max_cols) {
      if (line_idx + 1 >= max_lines) {
        break;
      }
      if (col > 0 && lines[line_idx][col - 1] == ' ') {
        --col;
      }
      lines[line_idx][col] = '\0';
      ++line_idx;
      col = 0;
      if (c == ' ') {
        continue;
      }
    }
    lines[line_idx][col++] = c;
  }
  if (col > 0 || line_idx == 0) {
    lines[line_idx][col] = '\0';
    ++line_idx;
  }
  return line_idx;
}

void draw_quote_panel(const PmHafezStatus &st) {
  const uint16_t c_panel = pm_gfx->color565(6, 8, 18);
  const uint16_t c_panel_edge = pm_gfx->color565(115, 125, 150);
  const uint16_t c_quote = pm_gfx->color565(235, 238, 246);
  const uint16_t c_meta = pm_gfx->color565(182, 188, 205);
  const uint16_t c_accent = pm_gfx->color565(252, 215, 146);

  pm_gfx->fillRect(28, 84, LCD_WIDTH - 56, 280, c_panel);
  pm_gfx->drawRect(28, 84, LCD_WIDTH - 56, 280, c_panel_edge);

  pm_face_draw_centered_line("HAFEZ", 96, c_accent, 1, 1);

  char lines[7][70];
  const int n = wrap_quote_lines(st.quote, lines, 7, 43);
  int y = 126;
  for (int i = 0; i < n; ++i) {
    pm_face_draw_centered_line(lines[i], y, c_quote, 1, 1);
    y += 22;
  }

  char source_line[96];
  if (st.source[0] != '\0') {
    snprintf(source_line, sizeof(source_line), "- %s", st.source);
  } else {
    snprintf(source_line, sizeof(source_line), "- Hafez");
  }
  pm_face_draw_centered_line(source_line, 304, c_meta, 1, 1);

  char art_line[84];
  if (st.art_prompt[0]) {
    snprintf(art_line, sizeof(art_line), "art: %.56s%s", st.art_prompt,
             strlen(st.art_prompt) > 56 ? "..." : "");
    pm_face_draw_centered_line(art_line, 328, c_meta, 1, 1);
  }
  if (st.palette[0]) {
    snprintf(art_line, sizeof(art_line), "palette: %s", st.palette);
    pm_face_draw_centered_line(art_line, 350, c_meta, 1, 1);
  }
}

}  // namespace

void pm_face_hafez_draw() {
  const uint16_t c_hi = pm_gfx->color565(230, 238, 255);
  const uint16_t c_dim = pm_gfx->color565(130, 136, 155);
  const uint16_t c_err = pm_gfx->color565(255, 132, 122);

  if (!pm_wifi_connected()) {
    pm_face_draw_centered_line("HAFEZ", 128, c_hi, 2, 2);
    pm_face_draw_centered_line("need WiFi", 182, c_dim, 1, 1);
    return;
  }
  if (!pm_time_valid()) {
    pm_face_draw_centered_line("HAFEZ", 128, c_hi, 2, 2);
    pm_face_draw_centered_line("need time", 182, c_dim, 1, 1);
    return;
  }
  if (!s_hafez_have_data) {
    pm_face_draw_centered_line("HAFEZ", 128, c_hi, 2, 2);
    pm_face_draw_centered_line("loading quote...", 182, c_dim, 1, 1);
    return;
  }
  if (!g_hafez_ui.ok) {
    pm_face_draw_centered_line("HAFEZ", 120, c_hi, 2, 2);
    if (!g_hafez_ui.configured) {
      pm_face_draw_centered_line("no quotes configured", 178, c_dim, 1, 1);
    } else {
      pm_face_draw_centered_line(g_hafez_ui.error[0] ? g_hafez_ui.error : "unavailable", 178, c_err, 1, 1);
    }
    return;
  }

  draw_generated_art(g_hafez_ui.art_seed, g_hafez_ui.palette);
  draw_quote_panel(g_hafez_ui);
}
