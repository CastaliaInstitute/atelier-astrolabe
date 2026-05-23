#include "faces/lenormand/pm_face_lenormand.h"

#include <Arduino_GFX_Library.h>
#include <cmath>
#include <cstdio>

#include "faces/alethiometer/pm_alethiometer_emoji_glyphs.h"
#include "faces/shared/pm_face_draw.h"
#include "pin_config.h"
#include "pm_display.h"

namespace {

constexpr int kCardCount = 36;
constexpr int kCx = LCD_WIDTH / 2;
constexpr int kCy = LCD_HEIGHT / 2;

struct LenormandCard {
  const char *title;
  const char *keyword;
  uint8_t glyph;
  uint8_t r;
  uint8_t g;
  uint8_t b;
};

const LenormandCard kCards[kCardCount] = {
    {"RIDER", "message", 34, 230, 176, 96},   {"CLOVER", "chance", 23, 116, 214, 134},
    {"SHIP", "passage", 16, 110, 190, 220},   {"HOUSE", "home", 29, 230, 190, 118},
    {"TREE", "roots", 10, 106, 194, 120},     {"CLOUDS", "unclear", 19, 160, 178, 190},
    {"SNAKE", "turning", 11, 176, 210, 96},   {"COFFIN", "ending", 4, 160, 142, 118},
    {"BOUQUET", "gift", 28, 230, 150, 188},   {"SCYTHE", "cut", 9, 230, 112, 92},
    {"WHIP", "friction", 17, 208, 126, 96},   {"BIRDS", "talk", 1, 238, 190, 86},
    {"CHILD", "new", 32, 156, 210, 238},      {"FOX", "strategy", 15, 224, 132, 80},
    {"BEAR", "power", 8, 218, 176, 90},       {"STARS", "guidance", 30, 172, 196, 255},
    {"STORK", "change", 34, 210, 220, 238},   {"DOG", "loyalty", 7, 238, 124, 146},
    {"TOWER", "structure", 13, 186, 160, 220}, {"GARDEN", "public", 12, 112, 206, 160},
    {"MOUNTAIN", "block", 20, 156, 176, 184}, {"CROSSROADS", "choice", 21, 214, 180, 96},
    {"MICE", "loss", 25, 168, 150, 132},      {"HEART", "love", 7, 238, 104, 132},
    {"RING", "bond", 31, 232, 198, 88},       {"BOOK", "hidden", 14, 142, 184, 228},
    {"LETTER", "news", 5, 222, 206, 150},     {"MAN", "querent", 18, 158, 206, 238},
    {"WOMAN", "querent", 18, 238, 158, 210},  {"LILY", "peace", 2, 232, 220, 152},
    {"SUN", "success", 2, 248, 206, 84},      {"MOON", "recognition", 3, 176, 178, 238},
    {"KEY", "answer", 5, 232, 196, 86},       {"FISH", "flow", 22, 102, 194, 220},
    {"ANCHOR", "stability", 6, 118, 184, 214}, {"CROSS", "burden", 26, 206, 176, 122},
};

int s_selected = -1;
int s_last_drawn = 0;

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

void draw_card_frame(int cx, int cy, const LenormandCard &card, int idx) {
  const uint16_t bg = pm_gfx->color565(18, 14, 20);
  const uint16_t paper = pm_gfx->color565(246, 235, 210);
  const uint16_t ink = pm_gfx->color565(36, 28, 32);
  const uint16_t accent = pm_gfx->color565(card.r, card.g, card.b);
  pm_gfx->fillRect(cx - 80, cy - 112, 160, 224, bg);
  pm_gfx->drawRect(cx - 80, cy - 112, 160, 224, accent);
  pm_gfx->drawRect(cx - 74, cy - 106, 148, 212, paper);
  pm_gfx->fillRect(cx - 68, cy - 100, 136, 200, paper);
  char num[8];
  snprintf(num, sizeof(num), "%02d", idx + 1);
  pm_face_draw_centered_line(num, cy - 90, accent, 1, 1);
  pm_alethiometer_draw_emoji_glyph_scaled(pm_gfx, cx, cy - 15, card.glyph, ink, 3);
  pm_face_draw_centered_line(card.title, cy + 62, ink, 2, 2);
  pm_face_draw_centered_line(card.keyword, cy + 88, blend565(paper, accent, 0.70f), 1, 1);
}

}  // namespace

int pm_face_lenormand_index(const struct tm *tm_local, bool valid_local) {
  if (s_selected >= 0) return s_selected;
  if (!valid_local || !tm_local) {
    return static_cast<int>((millis() / 60000u) % kCardCount);
  }
  const int yday = tm_local->tm_yday < 0 ? 0 : tm_local->tm_yday;
  const int year = tm_local->tm_year + 1900;
  return (yday + year * 11) % kCardCount;
}

bool pm_face_lenormand_cycle(int delta) {
  const int base = s_selected >= 0 ? s_selected : s_last_drawn;
  s_selected = (base + delta + kCardCount) % kCardCount;
  return true;
}

void pm_face_lenormand_reset_daily(void) { s_selected = -1; }

const char *pm_face_lenormand_title(int idx) {
  if (idx < 0) idx = 0;
  return kCards[idx % kCardCount].title;
}

const char *pm_face_lenormand_keyword(int idx) {
  if (idx < 0) idx = 0;
  return kCards[idx % kCardCount].keyword;
}

void pm_face_lenormand_draw(const struct tm *tm_local, bool valid_local) {
  const int idx = pm_face_lenormand_index(tm_local, valid_local);
  s_last_drawn = idx;
  const LenormandCard &card = kCards[idx];
  const uint16_t bg = pm_gfx->color565(10, 9, 15);
  const uint16_t accent = pm_gfx->color565(card.r, card.g, card.b);
  pm_gfx->fillScreen(bg);
  for (int i = 0; i < 36; ++i) {
    const float a = -pm_face_k_pi * 0.5f + static_cast<float>(i) * pm_face_k_two_pi / 36.f;
    const int x0 = kCx + static_cast<int>(lrintf(cosf(a) * 205.f));
    const int y0 = kCy + static_cast<int>(lrintf(sinf(a) * 205.f));
    const int x1 = kCx + static_cast<int>(lrintf(cosf(a) * 219.f));
    const int y1 = kCy + static_cast<int>(lrintf(sinf(a) * 219.f));
    pm_gfx->drawLine(x0, y0, x1, y1, i == idx ? accent : pm_gfx->color565(58, 48, 68));
  }
  pm_gfx->drawCircle(kCx, kCy, 222, blend565(bg, accent, 0.40f));
  pm_gfx->drawCircle(kCx, kCy, 204, pm_gfx->color565(58, 48, 68));
  draw_card_frame(kCx, kCy, card, idx);
  pm_face_draw_centered_line("LENORMAND", 38, accent, 2, 2);
  pm_face_draw_centered_line(s_selected >= 0 ? "swipe deck  tap daily" : "daily card", 410,
                             pm_gfx->color565(166, 154, 178), 1, 1);
}
