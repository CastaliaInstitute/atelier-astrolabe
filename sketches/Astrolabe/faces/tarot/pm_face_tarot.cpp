#include "faces/tarot/pm_face_tarot.h"

#include <Arduino_GFX_Library.h>
#include <cmath>
#include <cstdio>
#include <ctime>

#include "faces/shared/pm_face_draw.h"
#include "pin_config.h"
#include "pm_display.h"
#include "pm_wifi_ntp.h"

namespace {

constexpr int kCardCount = 22;
constexpr int kCx = LCD_WIDTH / 2;
constexpr int kCy = LCD_HEIGHT / 2;
constexpr const char *kManifestUrl = "https://tarot.castalia.institute/assets/major/manifest.json";

struct TarotCard {
  const char *title;
  const char *glyph;
  const char *theme;
  uint8_t r;
  uint8_t g;
  uint8_t b;
};

const TarotCard kCards[kCardCount] = {
    {"The Fool", "0", "begin", 244, 204, 90},
    {"The Magician", "I", "will", 220, 70, 64},
    {"High Priestess", "II", "veil", 78, 116, 210},
    {"The Empress", "III", "bloom", 88, 170, 98},
    {"The Emperor", "IV", "order", 196, 82, 54},
    {"The Hierophant", "V", "rite", 190, 170, 108},
    {"The Lovers", "VI", "choice", 225, 112, 142},
    {"The Chariot", "VII", "drive", 80, 132, 210},
    {"Strength", "VIII", "gentle", 238, 170, 76},
    {"The Hermit", "IX", "lamp", 170, 186, 205},
    {"Wheel of Fortune", "X", "turn", 214, 174, 72},
    {"Justice", "XI", "balance", 190, 82, 86},
    {"The Hanged Man", "XII", "pause", 92, 166, 190},
    {"Death", "XIII", "change", 210, 210, 210},
    {"Temperance", "XIV", "blend", 116, 184, 164},
    {"The Devil", "XV", "chain", 174, 64, 72},
    {"The Tower", "XVI", "break", 230, 144, 64},
    {"The Star", "XVII", "hope", 116, 174, 226},
    {"The Moon", "XVIII", "dream", 150, 150, 218},
    {"The Sun", "XIX", "joy", 248, 204, 74},
    {"Judgement", "XX", "call", 214, 130, 92},
    {"The World", "XXI", "whole", 116, 190, 142},
};

int s_selected = -1;

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

int daily_index(const struct tm *tm_local, bool valid_local) {
  if (valid_local && tm_local) {
    const int yday = tm_local->tm_yday >= 0 ? tm_local->tm_yday : 0;
    const int year = tm_local->tm_year + 1900;
    return (yday + year * 7) % kCardCount;
  }
  return static_cast<int>((millis() / 86400000u) % kCardCount);
}

void draw_star(int cx, int cy, int r_outer, int r_inner, int points, uint16_t col) {
  int px = 0;
  int py = 0;
  int first_x = 0;
  int first_y = 0;
  for (int i = 0; i < points * 2; ++i) {
    const float a = -pm_face_k_pi * 0.5f + static_cast<float>(i) * pm_face_k_pi / static_cast<float>(points);
    const int r = (i & 1) ? r_inner : r_outer;
    const int x = cx + static_cast<int>(lrintf(cosf(a) * r));
    const int y = cy + static_cast<int>(lrintf(sinf(a) * r));
    if (i == 0) {
      first_x = px = x;
      first_y = py = y;
    } else {
      pm_gfx->drawLine(px, py, x, y, col);
      px = x;
      py = y;
    }
  }
  pm_gfx->drawLine(px, py, first_x, first_y, col);
}

void draw_scales(int cx, int cy, uint16_t col) {
  pm_gfx->drawFastVLine(cx, cy - 48, 88, col);
  pm_gfx->drawFastHLine(cx - 55, cy - 22, 110, col);
  pm_gfx->drawLine(cx - 42, cy - 22, cx - 64, cy + 18, col);
  pm_gfx->drawLine(cx - 42, cy - 22, cx - 20, cy + 18, col);
  pm_gfx->drawLine(cx + 42, cy - 22, cx + 20, cy + 18, col);
  pm_gfx->drawLine(cx + 42, cy - 22, cx + 64, cy + 18, col);
  pm_gfx->drawCircle(cx - 42, cy + 22, 24, col);
  pm_gfx->drawCircle(cx + 42, cy + 22, 24, col);
}

void draw_card_symbol(int idx, int cx, int cy, uint16_t ink, uint16_t accent) {
  switch (idx) {
    case 0:
      pm_gfx->drawCircle(cx - 22, cy - 24, 14, ink);
      pm_gfx->drawLine(cx - 22, cy - 10, cx - 38, cy + 46, ink);
      pm_gfx->drawLine(cx - 22, cy - 10, cx + 18, cy + 28, ink);
      pm_gfx->drawCircle(cx + 46, cy + 46, 12, accent);
      break;
    case 1:
      pm_gfx->drawFastVLine(cx, cy - 62, 124, ink);
      pm_gfx->drawCircle(cx, cy - 72, 13, accent);
      pm_gfx->drawCircle(cx, cy + 72, 13, accent);
      pm_gfx->drawLine(cx - 46, cy, cx + 46, cy, ink);
      break;
    case 2:
      pm_gfx->fillCircle(cx, cy, 46, blend565(pm_gfx->color565(4, 8, 18), accent, 0.34f));
      pm_gfx->fillCircle(cx + 18, cy, 46, pm_gfx->color565(4, 8, 18));
      pm_gfx->drawCircle(cx, cy, 46, ink);
      break;
    case 3:
      for (int i = 0; i < 8; ++i) {
        const float a = static_cast<float>(i) * pm_face_k_two_pi / 8.f;
        pm_gfx->fillCircle(cx + static_cast<int>(cosf(a) * 36.f), cy + static_cast<int>(sinf(a) * 36.f), 18,
                           accent);
      }
      pm_gfx->fillCircle(cx, cy, 24, ink);
      break;
    case 4:
      pm_gfx->drawRect(cx - 50, cy - 48, 100, 96, ink);
      pm_gfx->drawFastHLine(cx - 66, cy - 50, 132, accent);
      pm_gfx->drawFastHLine(cx - 66, cy + 50, 132, accent);
      break;
    case 5:
      pm_gfx->drawTriangle(cx, cy - 68, cx - 58, cy + 42, cx + 58, cy + 42, ink);
      pm_gfx->drawCircle(cx, cy - 2, 25, accent);
      break;
    case 6:
      pm_gfx->drawCircle(cx - 28, cy - 8, 28, ink);
      pm_gfx->drawCircle(cx + 28, cy - 8, 28, accent);
      pm_gfx->drawLine(cx - 52, cy + 34, cx + 52, cy + 34, ink);
      break;
    case 7:
      pm_gfx->drawRect(cx - 58, cy - 30, 116, 60, ink);
      pm_gfx->drawCircle(cx - 34, cy + 44, 18, accent);
      pm_gfx->drawCircle(cx + 34, cy + 44, 18, accent);
      pm_gfx->drawTriangle(cx, cy - 74, cx - 22, cy - 30, cx + 22, cy - 30, ink);
      break;
    case 8:
      pm_gfx->drawCircle(cx - 28, cy, 32, ink);
      pm_gfx->drawCircle(cx + 28, cy, 32, ink);
      pm_gfx->drawCircle(cx, cy, 70, accent);
      break;
    case 9:
      pm_gfx->drawFastVLine(cx - 30, cy - 58, 116, ink);
      pm_gfx->drawCircle(cx + 20, cy - 32, 24, accent);
      pm_gfx->drawLine(cx - 30, cy - 18, cx + 20, cy - 32, ink);
      break;
    case 10:
      pm_gfx->drawCircle(cx, cy, 64, ink);
      for (int i = 0; i < 8; ++i) {
        const float a = static_cast<float>(i) * pm_face_k_two_pi / 8.f;
        pm_gfx->drawLine(cx, cy, cx + static_cast<int>(cosf(a) * 64.f), cy + static_cast<int>(sinf(a) * 64.f),
                         accent);
      }
      break;
    case 11:
      draw_scales(cx, cy, ink);
      break;
    case 12:
      pm_gfx->drawFastHLine(cx - 66, cy - 58, 132, ink);
      pm_gfx->drawFastVLine(cx, cy - 58, 112, accent);
      pm_gfx->drawCircle(cx, cy + 70, 16, ink);
      break;
    case 13:
      pm_gfx->drawCircle(cx, cy - 26, 28, ink);
      pm_gfx->drawFastHLine(cx - 38, cy + 2, 76, ink);
      pm_gfx->drawFastVLine(cx, cy + 2, 72, ink);
      pm_gfx->drawLine(cx - 26, cy + 74, cx + 26, cy + 74, accent);
      break;
    case 14:
      pm_gfx->drawCircle(cx - 34, cy - 20, 28, ink);
      pm_gfx->drawCircle(cx + 34, cy + 20, 28, accent);
      pm_gfx->drawLine(cx - 10, cy - 8, cx + 10, cy + 8, ink);
      break;
    case 15:
      pm_gfx->drawTriangle(cx, cy - 70, cx - 62, cy + 42, cx + 62, cy + 42, ink);
      pm_gfx->drawCircle(cx - 35, cy + 30, 18, accent);
      pm_gfx->drawCircle(cx + 35, cy + 30, 18, accent);
      break;
    case 16:
      pm_gfx->drawRect(cx - 28, cy - 62, 56, 112, ink);
      pm_gfx->drawLine(cx - 28, cy - 62, cx + 28, cy - 32, accent);
      pm_gfx->drawLine(cx + 28, cy - 32, cx - 18, cy + 10, accent);
      pm_gfx->drawLine(cx - 18, cy + 10, cx + 28, cy + 50, accent);
      break;
    case 17:
      draw_star(cx, cy, 72, 30, 8, ink);
      break;
    case 18:
      pm_gfx->drawCircle(cx - 28, cy - 20, 36, ink);
      pm_gfx->fillCircle(cx - 12, cy - 20, 36, pm_gfx->color565(7, 10, 18));
      pm_gfx->drawCircle(cx + 42, cy + 40, 20, accent);
      break;
    case 19:
      pm_gfx->fillCircle(cx, cy, 38, accent);
      for (int i = 0; i < 12; ++i) {
        const float a = static_cast<float>(i) * pm_face_k_two_pi / 12.f;
        pm_gfx->drawLine(cx + static_cast<int>(cosf(a) * 48.f), cy + static_cast<int>(sinf(a) * 48.f),
                         cx + static_cast<int>(cosf(a) * 76.f), cy + static_cast<int>(sinf(a) * 76.f), ink);
      }
      break;
    case 20:
      pm_gfx->drawTriangle(cx, cy - 64, cx - 66, cy + 36, cx + 66, cy + 36, accent);
      pm_gfx->drawCircle(cx, cy - 8, 34, ink);
      pm_gfx->drawFastHLine(cx - 50, cy + 60, 100, ink);
      break;
    default:
      pm_gfx->drawCircle(cx, cy, 70, ink);
      pm_gfx->drawCircle(cx, cy, 46, accent);
      pm_gfx->drawCircle(cx, cy, 22, ink);
      break;
  }
}

}  // namespace

const char *pm_face_tarot_manifest_url(void) { return kManifestUrl; }

const char *pm_face_tarot_title(int idx) {
  if (idx < 0 || idx >= kCardCount) {
    return "";
  }
  return kCards[idx].title;
}

int pm_face_tarot_index(const struct tm *tm_local, bool valid_local) {
  if (s_selected >= 0 && s_selected < kCardCount) {
    return s_selected;
  }
  return daily_index(tm_local, valid_local);
}

void pm_face_tarot_reset_daily(void) { s_selected = -1; }

bool pm_face_tarot_cycle(int delta) {
  struct tm tm = {};
  const bool valid = pm_time_valid();
  if (valid) {
    pm_time_local(&tm);
  }
  const int base = s_selected >= 0 ? s_selected : daily_index(&tm, valid);
  int v = (base + delta) % kCardCount;
  if (v < 0) {
    v += kCardCount;
  }
  s_selected = v;
  return true;
}

void pm_face_tarot_draw(const struct tm *tm_local, bool valid_local) {
  const int idx = pm_face_tarot_index(tm_local, valid_local);
  const TarotCard &card = kCards[idx];
  const uint16_t c_bg = pm_gfx->color565(7, 8, 14);
  const uint16_t c_panel = pm_gfx->color565(18, 16, 22);
  const uint16_t c_ink = pm_gfx->color565(238, 226, 196);
  const uint16_t c_dim = pm_gfx->color565(150, 136, 116);
  const uint16_t c_accent = pm_gfx->color565(card.r, card.g, card.b);
  const uint16_t c_glow = blend565(c_bg, c_accent, 0.22f);
  pm_gfx->fillScreen(c_bg);

  const int R = min(LCD_WIDTH, LCD_HEIGHT) / 2;
  for (int r = R - 6; r > 28; r -= 7) {
    const float a = static_cast<float>(r - 28) / static_cast<float>(R - 34);
    pm_gfx->drawCircle(kCx, kCy, r, blend565(c_bg, c_glow, a * 0.32f));
  }

  pm_gfx->fillCircle(kCx, kCy, 160, c_panel);
  pm_gfx->drawCircle(kCx, kCy, 160, c_accent);
  pm_gfx->drawCircle(kCx, kCy, 154, blend565(c_panel, c_accent, 0.38f));

  for (int i = 0; i < kCardCount; ++i) {
    const float deg = static_cast<float>(i) * 360.f / static_cast<float>(kCardCount);
    const float ang = pm_face_deg_to_rad(deg);
    const int r0 = (i == idx) ? 168 : 174;
    const int r1 = (i == idx) ? 190 : 184;
    const uint16_t col = (i == idx) ? c_accent : pm_gfx->color565(54, 48, 42);
    pm_gfx->drawLine(kCx + static_cast<int>(lrintf(cosf(ang) * r0)),
                     kCy + static_cast<int>(lrintf(sinf(ang) * r0)),
                     kCx + static_cast<int>(lrintf(cosf(ang) * r1)),
                     kCy + static_cast<int>(lrintf(sinf(ang) * r1)), col);
  }

  draw_card_symbol(idx, kCx, kCy - 6, c_ink, c_accent);

  char num[8];
  snprintf(num, sizeof(num), "%02d", idx);
  pm_face_draw_centered_line(num, 38, c_accent, 2, 2);
  pm_face_draw_centered_line(card.title, 340, c_ink, 2, 2);
  pm_face_draw_centered_line(card.theme, 374, c_dim, 1, 1);

  if (s_selected < 0) {
    pm_face_draw_centered_line("daily major", 408, c_dim, 1, 1);
  } else {
    pm_face_draw_centered_line("swipe deck  tap daily", 408, c_dim, 1, 1);
  }
}
