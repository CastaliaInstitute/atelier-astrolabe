#include "faces/human_design/pm_face_human_design.h"

#include <Arduino_GFX_Library.h>
#include <cmath>
#include <cstdio>

#include "faces/shared/pm_face_draw.h"
#include "pm_display.h"

namespace {

constexpr int kCx = pm_face_lcd_cx;
constexpr int kCy = pm_face_lcd_cy;

struct CenterNode {
  const char *label;
  int x;
  int y;
  uint8_t r;
  uint8_t g;
  uint8_t b;
  bool defined;
};

struct ChannelLine {
  int a;
  int b;
  const char *gates;
};

struct GateLine {
  const char *body;
  uint8_t gate;
  uint8_t line;
};

struct HumanDesignPalette {
  uint16_t bg;
  uint16_t text;
  uint16_t subtext;
  uint16_t dim;
  uint16_t ring;
  uint16_t inner_ring;
  uint16_t accent;
  uint16_t design;
  uint16_t personality;
  uint16_t center_defined_mix;
  uint16_t center_undefined;
  uint16_t center_rim;
  uint16_t center_text;
  uint16_t channel_dark;
  uint16_t gate_fill;
  uint16_t gate_border;
  uint16_t gate_text;
  uint16_t human_halo;
  uint16_t human_body;
  uint16_t human_edge;
};

enum CenterIndex : uint8_t {
  kHead = 0,
  kAjna,
  kThroat,
  kG,
  kEgo,
  kSolar,
  kSpleen,
  kSacral,
  kRoot,
  kCenterCount,
};

const CenterNode kCenters[kCenterCount] = {
    {"HEAD", 233, 80, 186, 152, 226, false},   {"AJNA", 233, 121, 160, 186, 232, false},
    {"THROAT", 233, 168, 104, 196, 224, true}, {"G", 233, 226, 236, 188, 86, true},
    {"EGO", 286, 242, 214, 128, 92, false},    {"SP", 294, 310, 198, 102, 184, false},
    {"SPL", 172, 310, 94, 188, 140, true},     {"SAC", 233, 318, 226, 158, 74, true},
    {"ROOT", 233, 369, 206, 86, 86, true},
};

const ChannelLine kChannels[] = {
    {kThroat, kG, "1-8"},    {kThroat, kG, "7-31"},  {kThroat, kSpleen, "16-48"},
    {kG, kSacral, "10-34"},  {kSacral, kSpleen, "34-57"}, {kSacral, kRoot, "3-60"},
};

const GateLine kPersonality[] = {
    {"SUN", 2, 4},  {"EAR", 1, 4},  {"NOD", 60, 3}, {"MOO", 13, 4}, {"MER", 51, 6},
    {"VEN", 12, 6}, {"MAR", 12, 4}, {"JUP", 58, 5}, {"SAT", 16, 2}, {"URA", 48, 6},
    {"NEP", 34, 5}, {"PLU", 46, 2},
};

const GateLine kDesign[] = {
    {"SUN", 13, 6}, {"EAR", 7, 6},  {"NOD", 41, 4}, {"MOO", 43, 3}, {"MER", 19, 4},
    {"VEN", 36, 5}, {"MAR", 3, 2},  {"JUP", 10, 3}, {"SAT", 8, 6},  {"URA", 57, 4},
    {"NEP", 34, 6}, {"PLU", 46, 4},
};

uint16_t blend565(uint16_t bg, uint16_t fg, float alpha) {
  if (alpha <= 0.f) return bg;
  if (alpha >= 1.f) return fg;
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

void draw_text_centered_at(const char *text, int cx, int y, uint16_t col, uint8_t size = 1) {
  if (!text) return;
  int len = 0;
  while (text[len] != '\0') {
    ++len;
  }
  pm_gfx->setTextColor(col);
  pm_gfx->setTextSize(size);
  pm_gfx->setCursor(cx - (len * 6 * size) / 2, y);
  pm_gfx->print(text);
}

void draw_diamond(int x, int y, int r, uint16_t fill, uint16_t rim) {
  pm_gfx->fillTriangle(x, y - r, x + r, y, x, y + r, fill);
  pm_gfx->fillTriangle(x, y - r, x - r, y, x, y + r, fill);
  pm_gfx->drawLine(x, y - r, x + r, y, rim);
  pm_gfx->drawLine(x + r, y, x, y + r, rim);
  pm_gfx->drawLine(x, y + r, x - r, y, rim);
  pm_gfx->drawLine(x - r, y, x, y - r, rim);
}

void draw_center(const CenterNode &c, int idx, const HumanDesignPalette &pal) {
  const uint16_t base = pm_gfx->color565(c.r, c.g, c.b);
  const uint16_t fill = c.defined ? blend565(base, pal.center_defined_mix, 0.58f) : pal.center_undefined;
  const uint16_t rim = pal.center_rim;
  if (idx == kHead) {
    pm_gfx->fillTriangle(c.x, c.y - 22, c.x - 30, c.y + 22, c.x + 30, c.y + 22, fill);
    pm_gfx->drawTriangle(c.x, c.y - 22, c.x - 30, c.y + 22, c.x + 30, c.y + 22, rim);
  } else if (idx == kAjna) {
    pm_gfx->fillTriangle(c.x - 31, c.y - 21, c.x + 31, c.y - 21, c.x, c.y + 23, fill);
    pm_gfx->drawTriangle(c.x - 31, c.y - 21, c.x + 31, c.y - 21, c.x, c.y + 23, rim);
  } else if (idx == kG) {
    draw_diamond(c.x, c.y, 35, fill, rim);
  } else if (idx == kSacral || idx == kRoot || idx == kThroat) {
    pm_gfx->fillRect(c.x - 33, c.y - 24, 66, 48, fill);
    pm_gfx->drawRect(c.x - 33, c.y - 24, 66, 48, rim);
  } else if (idx == kEgo) {
    pm_gfx->fillTriangle(c.x - 23, c.y - 22, c.x + 27, c.y, c.x - 18, c.y + 24, fill);
    pm_gfx->drawTriangle(c.x - 23, c.y - 22, c.x + 27, c.y, c.x - 18, c.y + 24, rim);
  } else {
    pm_gfx->fillTriangle(c.x - 25, c.y - 28, c.x - 25, c.y + 28, c.x + 28, c.y, fill);
    pm_gfx->drawTriangle(c.x - 25, c.y - 28, c.x - 25, c.y + 28, c.x + 28, c.y, rim);
  }
  draw_text_centered_at(c.label, c.x, c.y - 4, pal.center_text);
}

void draw_channels(const HumanDesignPalette &pal) {
  int channel_idx = 0;
  for (const ChannelLine &ch : kChannels) {
    const CenterNode &a = kCenters[ch.a];
    const CenterNode &b = kCenters[ch.b];
    const uint16_t col = (channel_idx++ % 2) == 0 ? pal.channel_dark : pal.accent;
    pm_gfx->drawLine(a.x, a.y, b.x, b.y, col);
    pm_gfx->drawLine(a.x + 1, a.y, b.x + 1, b.y, col);
    pm_gfx->drawLine(a.x - 1, a.y, b.x - 1, b.y, col);
    const int mx = (a.x + b.x) / 2;
    const int my = (a.y + b.y) / 2;
    pm_gfx->fillCircle(mx, my, 4, col);
    pm_gfx->drawCircle(mx, my, 7, blend565(pal.bg, col, 0.55f));
  }
}

void draw_gate_pill(const GateLine &gate, int cx, int cy, uint16_t col, const HumanDesignPalette &pal) {
  char buf[8];
  snprintf(buf, sizeof(buf), "%u.%u", gate.gate, gate.line);
  pm_gfx->fillRoundRect(cx - 17, cy - 8, 34, 16, 5, pal.gate_fill);
  pm_gfx->drawRoundRect(cx - 17, cy - 8, 34, 16, 5, pal.gate_border);
  pm_gfx->fillCircle(cx - 22, cy, 3, col);
  draw_text_centered_at(buf, cx, cy - 4, pal.gate_text);
}

void draw_gate_arc(const GateLine *gates, int count, float start_deg, float end_deg, int radius, uint16_t col,
                   const char *label, int label_x, const HumanDesignPalette &pal) {
  if (!gates || count <= 0) {
    return;
  }
  const uint16_t rail = blend565(pal.bg, col, 0.42f);
  const int segments = 36;
  int prev_x = 0;
  int prev_y = 0;
  for (int i = 0; i <= segments; ++i) {
    const float t = static_cast<float>(i) / static_cast<float>(segments);
    const float deg = start_deg + (end_deg - start_deg) * t;
    const float a = deg * (pm_face_k_pi / 180.f);
    const int x = kCx + static_cast<int>(lrintf(cosf(a) * radius));
    const int y = kCy + static_cast<int>(lrintf(sinf(a) * radius));
    if (i > 0) {
      pm_gfx->drawLine(prev_x, prev_y, x, y, rail);
    }
    prev_x = x;
    prev_y = y;
  }

  draw_text_centered_at(label, label_x, 51, col);
  for (int i = 0; i < count; ++i) {
    const float t = count == 1 ? 0.5f : static_cast<float>(i) / static_cast<float>(count - 1);
    const float deg = start_deg + (end_deg - start_deg) * t;
    const float a = deg * (pm_face_k_pi / 180.f);
    const int x = kCx + static_cast<int>(lrintf(cosf(a) * radius));
    const int y = kCy + static_cast<int>(lrintf(sinf(a) * radius));
    draw_gate_pill(gates[i], x, y, col, pal);
  }
}

void draw_limb(int x0, int y0, int x1, int y1, uint16_t col) {
  pm_gfx->drawLine(x0, y0, x1, y1, col);
  pm_gfx->drawLine(x0 + 1, y0, x1 + 1, y1, col);
  pm_gfx->drawLine(x0 - 1, y0, x1 - 1, y1, col);
}

void draw_human_form(const HumanDesignPalette &pal) {
  const uint16_t halo = pal.human_halo;
  const uint16_t body = pal.human_body;
  const uint16_t edge = pal.human_edge;

  // Soft continuous human silhouette, inspired by printed Human Design charts.
  pm_gfx->fillCircle(kCx, 94, 38, halo);
  pm_gfx->fillRoundRect(kCx - 20, 118, 40, 46, 18, body);
  pm_gfx->fillEllipse(kCx - 62, 214, 82, 96, body);
  pm_gfx->fillEllipse(kCx + 62, 214, 82, 96, body);
  pm_gfx->fillEllipse(kCx, 274, 92, 128, body);
  pm_gfx->fillTriangle(kCx - 92, 188, kCx - 142, 328, kCx - 52, 336, body);
  pm_gfx->fillTriangle(kCx + 92, 188, kCx + 142, 328, kCx + 52, 336, body);
  pm_gfx->fillTriangle(kCx - 48, 356, kCx - 18, 410, kCx - 84, 410, body);
  pm_gfx->fillTriangle(kCx + 48, 356, kCx + 18, 410, kCx + 84, 410, body);

  pm_gfx->drawCircle(kCx, 94, 38, edge);
  pm_gfx->drawLine(kCx - 18, 128, kCx - 58, 166, edge);
  pm_gfx->drawLine(kCx + 18, 128, kCx + 58, 166, edge);
  pm_gfx->drawLine(kCx - 58, 166, kCx - 132, 326, edge);
  pm_gfx->drawLine(kCx + 58, 166, kCx + 132, 326, edge);
  pm_gfx->drawLine(kCx - 132, 326, kCx - 50, 350, edge);
  pm_gfx->drawLine(kCx + 132, 326, kCx + 50, 350, edge);
  pm_gfx->drawLine(kCx - 50, 350, kCx - 80, 410, edge);
  pm_gfx->drawLine(kCx + 50, 350, kCx + 80, 410, edge);
  pm_gfx->drawLine(kCx - 20, 410, kCx - 80, 410, edge);
  pm_gfx->drawLine(kCx + 20, 410, kCx + 80, 410, edge);
}

void draw_orbital_ticks(uint16_t dim, uint16_t accent) {
  for (int i = 0; i < 64; ++i) {
    const float a = -pm_face_k_pi * 0.5f + static_cast<float>(i) * pm_face_k_two_pi / 64.f;
    const int r0 = (i % 8 == 0) ? 207 : 214;
    const int r1 = 222;
    const int x0 = kCx + static_cast<int>(lrintf(cosf(a) * r0));
    const int y0 = kCy + static_cast<int>(lrintf(sinf(a) * r0));
    const int x1 = kCx + static_cast<int>(lrintf(cosf(a) * r1));
    const int y1 = kCy + static_cast<int>(lrintf(sinf(a) * r1));
    pm_gfx->drawLine(x0, y0, x1, y1, (i == 1 || i == 2 || i == 7 || i == 12 || i == 13 || i == 16 ||
                                      i == 34 || i == 48 || i == 57 || i == 60)
                                         ? accent
                                         : dim);
  }
}

}  // namespace

void pm_face_human_design_draw() {
  const HumanDesignPalette pal = {
      pm_gfx->color565(9, 12, 15),       // bg
      pm_gfx->color565(226, 222, 208),   // text
      pm_gfx->color565(158, 166, 164),   // subtext
      pm_gfx->color565(48, 57, 61),      // dim
      pm_gfx->color565(68, 78, 78),      // ring
      pm_gfx->color565(33, 42, 45),      // inner_ring
      pm_gfx->color565(222, 151, 82),    // accent
      pm_gfx->color565(95, 188, 176),    // design
      pm_gfx->color565(222, 112, 92),    // personality
      pm_gfx->color565(205, 142, 78),    // center_defined_mix
      pm_gfx->color565(22, 28, 30),      // center_undefined
      pm_gfx->color565(210, 204, 184),   // center_rim
      pm_gfx->color565(244, 239, 220),   // center_text
      pm_gfx->color565(236, 232, 212),   // channel_dark
      pm_gfx->color565(23, 29, 31),      // gate_fill
      pm_gfx->color565(86, 91, 85),      // gate_border
      pm_gfx->color565(232, 226, 210),   // gate_text
      pm_gfx->color565(19, 25, 27),      // human_halo
      pm_gfx->color565(16, 22, 24),      // human_body
      pm_gfx->color565(78, 91, 91),      // human_edge
  };

  pm_gfx->fillScreen(pal.bg);
  draw_orbital_ticks(pal.dim, pal.accent);
  pm_gfx->drawCircle(kCx, kCy, 224, pal.ring);
  pm_gfx->drawCircle(kCx, kCy, 198, pal.inner_ring);

  pm_face_draw_centered_line("Human Design", 12, pal.text, 1, 1);
  pm_face_draw_centered_line("Daniel McShan", 28, pal.accent, 1, 1);
  pm_face_draw_centered_line("MG  |  Sacral  |  4/6", 434, pal.text, 1, 1);
  pm_face_draw_centered_line("Single Definition", 449, pal.design, 1, 1);

  draw_human_form(pal);
  draw_gate_arc(kDesign, static_cast<int>(sizeof(kDesign) / sizeof(kDesign[0])), 126.f, 234.f, 188, pal.design,
                "DESIGN", 94, pal);
  draw_gate_arc(kPersonality, static_cast<int>(sizeof(kPersonality) / sizeof(kPersonality[0])), -54.f, 54.f,
                188, pal.personality, "PERSONALITY", 372, pal);

  draw_channels(pal);
  for (int i = 0; i < kCenterCount; ++i) {
    draw_center(kCenters[i], i, pal);
  }

  pm_face_draw_centered_line("Sphinx 2/1 13/7", 419, pal.subtext, 1, 1);
}
