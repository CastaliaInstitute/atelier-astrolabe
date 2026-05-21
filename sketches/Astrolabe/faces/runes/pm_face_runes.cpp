#include "faces/runes/pm_face_runes.h"

#include <Arduino_GFX_Library.h>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "faces/shared/pm_face_draw.h"
#include "pin_config.h"
#include "pm_display.h"

namespace {

constexpr int kRuneCount = 24;
constexpr int kSpreadCount = 3;
constexpr int kCx = LCD_WIDTH / 2;
constexpr int kCy = LCD_HEIGHT / 2;
constexpr int kTokenR = 68;

struct Rune {
  const char *name;
  const char *sound;
  const char *keyword;
};

const Rune kRunes[kRuneCount] = {
    {"Fehu", "F", "resource"},      {"Uruz", "U", "strength"},
    {"Thurisaz", "TH", "threshold"}, {"Ansuz", "A", "message"},
    {"Raidho", "R", "journey"},     {"Kenaz", "K", "torch"},
    {"Gebo", "G", "gift"},          {"Wunjo", "W", "joy"},
    {"Hagalaz", "H", "storm"},      {"Nauthiz", "N", "need"},
    {"Isa", "I", "stillness"},      {"Jera", "J", "harvest"},
    {"Eihwaz", "EI", "endurance"},  {"Perthro", "P", "chance"},
    {"Algiz", "Z", "protection"},   {"Sowilo", "S", "sun"},
    {"Tiwaz", "T", "justice"},      {"Berkano", "B", "growth"},
    {"Ehwaz", "E", "trust"},        {"Mannaz", "M", "self"},
    {"Laguz", "L", "flow"},         {"Ingwaz", "NG", "seed"},
    {"Dagaz", "D", "daybreak"},     {"Othala", "O", "inheritance"},
};

const char *kSlots[kSpreadCount] = {"past", "present", "future"};
int s_spread[kSpreadCount] = {0, 10, 22};
uint32_t s_cast_nonce = 0;
bool s_seeded = false;

struct RunePose {
  int x;
  int y;
  float rot;
};

RunePose s_pose[kSpreadCount] = {{88, 238, -0.12f}, {240, 220, 0.08f}, {392, 238, 0.16f}};

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

uint32_t daily_seed(const struct tm *tm_local, bool valid_local) {
  if (valid_local && tm_local) {
    return static_cast<uint32_t>((tm_local->tm_year + 1900) * 372 + tm_local->tm_yday * 13 +
                                 tm_local->tm_mon * 31 + tm_local->tm_mday);
  }
  return millis() / 86400000u;
}

uint32_t mix32(uint32_t x) {
  x ^= x >> 16;
  x *= 0x7feb352du;
  x ^= x >> 15;
  x *= 0x846ca68bu;
  x ^= x >> 16;
  return x;
}

void seed_spread(uint32_t seed) {
  bool used[kRuneCount] = {};
  for (int i = 0; i < kSpreadCount; ++i) {
    int idx = static_cast<int>(mix32(seed + static_cast<uint32_t>(i) * 0x9e3779b9u) % kRuneCount);
    while (used[idx]) {
      idx = (idx + 1) % kRuneCount;
    }
    used[idx] = true;
    s_spread[i] = idx;
  }
  const int base_x[kSpreadCount] = {88, 240, 392};
  const int base_y[kSpreadCount] = {238, 218, 238};
  for (int i = 0; i < kSpreadCount; ++i) {
    const uint32_t h = mix32(seed ^ (0x31f4a7u + static_cast<uint32_t>(i) * 0x9e3779b9u));
    const int jitter_x = static_cast<int>((h >> 3) % 9u) - 4;
    const int jitter_y = static_cast<int>((h >> 9) % 37u) - 18;
    s_pose[i].x = base_x[i] + jitter_x;
    s_pose[i].y = base_y[i] + jitter_y;
    s_pose[i].rot = (static_cast<float>(static_cast<int>((h >> 17) % 61u) - 30) * pm_face_k_pi) / 180.f;
  }
  s_seeded = true;
}

void ensure_spread(const struct tm *tm_local, bool valid_local) {
  if (!s_seeded) {
    seed_spread(daily_seed(tm_local, valid_local));
  }
}

void draw_bind_line(int x0, int y0, int x1, int y1, uint16_t col, int w) {
  for (int i = -w; i <= w; ++i) {
    pm_gfx->drawLine(x0 + i, y0, x1 + i, y1, col);
    pm_gfx->drawLine(x0, y0 + i, x1, y1 + i, col);
  }
}

void rotate_point(int cx, int cy, int lx, int ly, float rot, int *x, int *y) {
  const float c = cosf(rot);
  const float s = sinf(rot);
  *x = cx + static_cast<int>(lrintf(static_cast<float>(lx) * c - static_cast<float>(ly) * s));
  *y = cy + static_cast<int>(lrintf(static_cast<float>(lx) * s + static_cast<float>(ly) * c));
}

void draw_rot_line(int cx, int cy, int x0, int y0, int x1, int y1, float rot, uint16_t col, int w) {
  int ax = 0;
  int ay = 0;
  int bx = 0;
  int by = 0;
  rotate_point(cx, cy, x0, y0, rot, &ax, &ay);
  rotate_point(cx, cy, x1, y1, rot, &bx, &by);
  draw_bind_line(ax, ay, bx, by, col, w);
}

void draw_rune_glyph(int idx, int cx, int cy, uint16_t col) {
  const int h = 76;
  const int y0 = cy - h / 2;
  const int y1 = cy + h / 2;
  draw_bind_line(cx, y0, cx, y1, col, 1);
  switch (idx % 12) {
    case 0:
      draw_bind_line(cx, y0 + 6, cx + 30, cy - 12, col, 1);
      draw_bind_line(cx, cy - 4, cx + 25, cy + 12, col, 1);
      break;
    case 1:
      draw_bind_line(cx, y0, cx - 28, cy, col, 1);
      draw_bind_line(cx - 28, cy, cx, y1, col, 1);
      draw_bind_line(cx, y0, cx + 28, cy, col, 1);
      draw_bind_line(cx + 28, cy, cx, y1, col, 1);
      break;
    case 2:
      draw_bind_line(cx - 26, y0, cx + 26, y1, col, 1);
      draw_bind_line(cx + 26, y0, cx - 26, y1, col, 1);
      break;
    case 3:
      draw_bind_line(cx, y0 + 8, cx + 30, cy - 10, col, 1);
      draw_bind_line(cx, cy - 2, cx + 30, cy + 18, col, 1);
      break;
    case 4:
      draw_bind_line(cx - 28, y0 + 8, cx + 24, y1 - 8, col, 1);
      draw_bind_line(cx - 24, cy + 8, cx + 28, cy - 8, col, 1);
      break;
    case 5:
      draw_bind_line(cx - 26, cy + 6, cx + 26, cy - 24, col, 1);
      draw_bind_line(cx - 26, cy + 24, cx + 26, cy - 6, col, 1);
      break;
    case 6:
      draw_bind_line(cx - 30, y0 + 6, cx + 30, y1 - 6, col, 1);
      draw_bind_line(cx + 30, y0 + 6, cx - 30, y1 - 6, col, 1);
      break;
    case 7:
      draw_bind_line(cx, cy, cx - 30, y0 + 6, col, 1);
      draw_bind_line(cx, cy, cx + 30, y0 + 6, col, 1);
      break;
    case 8:
      draw_bind_line(cx - 26, y0 + 10, cx + 26, y1 - 10, col, 1);
      break;
    case 9:
      draw_bind_line(cx, cy - 6, cx + 28, y0 + 12, col, 1);
      draw_bind_line(cx, cy + 8, cx - 28, y1 - 12, col, 1);
      break;
    case 10:
      pm_gfx->drawFastHLine(cx - 32, cy, 64, col);
      pm_gfx->drawFastHLine(cx - 32, cy + 1, 64, col);
      break;
    default:
      draw_bind_line(cx, cy - 4, cx + 28, y0 + 12, col, 1);
      draw_bind_line(cx, cy + 4, cx - 28, y1 - 12, col, 1);
      break;
  }
}

void draw_rune_glyph_rot(int idx, int cx, int cy, float rot, uint16_t col) {
  const int h = 76;
  const int y0 = -h / 2;
  const int y1 = h / 2;
  draw_rot_line(cx, cy, 0, y0, 0, y1, rot, col, 1);
  switch (idx % 12) {
    case 0:
      draw_rot_line(cx, cy, 0, y0 + 6, 30, -12, rot, col, 1);
      draw_rot_line(cx, cy, 0, -4, 25, 12, rot, col, 1);
      break;
    case 1:
      draw_rot_line(cx, cy, 0, y0, -28, 0, rot, col, 1);
      draw_rot_line(cx, cy, -28, 0, 0, y1, rot, col, 1);
      draw_rot_line(cx, cy, 0, y0, 28, 0, rot, col, 1);
      draw_rot_line(cx, cy, 28, 0, 0, y1, rot, col, 1);
      break;
    case 2:
      draw_rot_line(cx, cy, -26, y0, 26, y1, rot, col, 1);
      draw_rot_line(cx, cy, 26, y0, -26, y1, rot, col, 1);
      break;
    case 3:
      draw_rot_line(cx, cy, 0, y0 + 8, 30, -10, rot, col, 1);
      draw_rot_line(cx, cy, 0, -2, 30, 18, rot, col, 1);
      break;
    case 4:
      draw_rot_line(cx, cy, -28, y0 + 8, 24, y1 - 8, rot, col, 1);
      draw_rot_line(cx, cy, -24, 8, 28, -8, rot, col, 1);
      break;
    case 5:
      draw_rot_line(cx, cy, -26, 6, 26, -24, rot, col, 1);
      draw_rot_line(cx, cy, -26, 24, 26, -6, rot, col, 1);
      break;
    case 6:
      draw_rot_line(cx, cy, -30, y0 + 6, 30, y1 - 6, rot, col, 1);
      draw_rot_line(cx, cy, 30, y0 + 6, -30, y1 - 6, rot, col, 1);
      break;
    case 7:
      draw_rot_line(cx, cy, 0, 0, -30, y0 + 6, rot, col, 1);
      draw_rot_line(cx, cy, 0, 0, 30, y0 + 6, rot, col, 1);
      break;
    case 8:
      draw_rot_line(cx, cy, -26, y0 + 10, 26, y1 - 10, rot, col, 1);
      break;
    case 9:
      draw_rot_line(cx, cy, 0, -6, 28, y0 + 12, rot, col, 1);
      draw_rot_line(cx, cy, 0, 8, -28, y1 - 12, rot, col, 1);
      break;
    case 10:
      draw_rot_line(cx, cy, -32, 0, 32, 0, rot, col, 1);
      break;
    default:
      draw_rot_line(cx, cy, 0, -4, 28, y0 + 12, rot, col, 1);
      draw_rot_line(cx, cy, 0, 4, -28, y1 - 12, rot, col, 1);
      break;
  }
}

void draw_wood_grain(int cx, int cy, int r, uint16_t line, uint16_t dark) {
  for (int y = -r + 10; y <= r - 10; y += 13) {
    const int half = static_cast<int>(sqrtf(static_cast<float>(r * r - y * y))) - 8;
    if (half <= 0) {
      continue;
    }
    const int wobble = static_cast<int>(sinf(static_cast<float>(y + cx) * 0.075f) * 8.f);
    pm_gfx->drawFastHLine(cx - half + wobble, cy + y, half * 2 - abs(wobble), line);
  }
  for (int rr = r - 13; rr > 16; rr -= 15) {
    pm_gfx->drawCircle(cx + (rr % 2 ? 4 : -5), cy + (rr % 3 ? -2 : 5), rr / 2, dark);
  }
}

float token_lump(int slot, int i) {
  const float a = static_cast<float>(i) * pm_face_k_two_pi / 40.f;
  return sinf(a * 3.f + static_cast<float>(slot) * 0.9f) * 3.8f +
         sinf(a * 7.f + static_cast<float>(slot) * 1.7f) * 2.2f +
         sinf(a * 13.f + static_cast<float>(slot) * 0.4f) * 1.1f;
}

void draw_organic_ring(int slot, int cx, int cy, int r, uint16_t fill, uint16_t edge, uint16_t bark) {
  constexpr int kPts = 40;
  int px = 0;
  int py = 0;
  int first_x = 0;
  int first_y = 0;
  for (int rr = 0; rr <= r; rr += 3) {
    uint16_t col = rr > r - 11 ? bark : fill;
    for (int i = 0; i < kPts; ++i) {
      const float a = static_cast<float>(i) * pm_face_k_two_pi / static_cast<float>(kPts);
      const int local_r = rr + (rr > r - 14 ? static_cast<int>(lrintf(token_lump(slot, i))) : 0);
      const int x = cx + static_cast<int>(lrintf(cosf(a) * static_cast<float>(local_r)));
      const int y = cy + static_cast<int>(lrintf(sinf(a) * static_cast<float>(local_r)));
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
  for (int i = 0; i < kPts; ++i) {
    const float a0 = static_cast<float>(i) * pm_face_k_two_pi / static_cast<float>(kPts);
    const float a1 = static_cast<float>(i + 1) * pm_face_k_two_pi / static_cast<float>(kPts);
    const int r0 = r + static_cast<int>(lrintf(token_lump(slot, i)));
    const int r1 = r + static_cast<int>(lrintf(token_lump(slot, i + 1)));
    const int x0 = cx + static_cast<int>(lrintf(cosf(a0) * static_cast<float>(r0)));
    const int y0 = cy + static_cast<int>(lrintf(sinf(a0) * static_cast<float>(r0)));
    const int x1 = cx + static_cast<int>(lrintf(cosf(a1) * static_cast<float>(r1)));
    const int y1 = cy + static_cast<int>(lrintf(sinf(a1) * static_cast<float>(r1)));
    pm_gfx->drawLine(x0, y0, x1, y1, edge);
  }
  for (int i = 0; i < 16; ++i) {
    const float a = static_cast<float>(i) * pm_face_k_two_pi / 16.f + static_cast<float>(slot) * 0.21f;
    const int r0 = r - 13 + (i % 3);
    const int r1 = r + 4 - (i % 2);
    const int x0 = cx + static_cast<int>(lrintf(cosf(a) * static_cast<float>(r0)));
    const int y0 = cy + static_cast<int>(lrintf(sinf(a) * static_cast<float>(r0)));
    const int x1 = cx + static_cast<int>(lrintf(cosf(a) * static_cast<float>(r1)));
    const int y1 = cy + static_cast<int>(lrintf(sinf(a) * static_cast<float>(r1)));
    pm_gfx->drawLine(x0, y0, x1, y1, bark);
  }
}

void draw_carved_rune(int idx, int cx, int cy, float rot) {
  const uint16_t cut_shadow = pm_gfx->color565(70, 38, 20);
  const uint16_t cut = pm_gfx->color565(28, 16, 12);
  const uint16_t lip = pm_gfx->color565(216, 158, 92);
  draw_rune_glyph_rot(idx, cx + 2, cy + 2, rot, cut_shadow);
  draw_rune_glyph_rot(idx, cx, cy, rot, cut);
  draw_rune_glyph_rot(idx, cx - 1, cy - 1, rot, lip);
  draw_rune_glyph_rot(idx, cx, cy, rot, cut);
}

void draw_card(int slot, int cx, int cy, float rot) {
  const int idx = s_spread[slot];
  const uint16_t bg = pm_gfx->color565(12, 10, 16);
  const uint16_t wood = slot == 0 ? pm_gfx->color565(126, 76, 38)
                                  : (slot == 1 ? pm_gfx->color565(156, 96, 44)
                                               : pm_gfx->color565(112, 82, 54));
  const uint16_t wood_hi = pm_gfx->color565(202, 140, 76);
  const uint16_t wood_dark = pm_gfx->color565(74, 42, 24);
  const uint16_t edge = slot == 0 ? pm_gfx->color565(190, 132, 76)
                                  : (slot == 1 ? pm_gfx->color565(226, 166, 86)
                                               : pm_gfx->color565(172, 128, 80));
  const uint16_t ink = pm_gfx->color565(238, 220, 168);
  const uint16_t dim = pm_gfx->color565(164, 146, 116);
  pm_gfx->fillCircle(cx + 4, cy + 5, kTokenR, blend565(bg, RGB565_BLACK, 0.42f));
  draw_organic_ring(slot, cx, cy, kTokenR, wood_dark, edge, pm_gfx->color565(48, 28, 18));
  draw_organic_ring(slot + 3, cx - 2, cy - 3, 59, wood, blend565(wood, wood_hi, 0.35f),
                    blend565(wood, wood_dark, 0.50f));
  pm_gfx->fillCircle(cx - 18, cy - 22, 18, blend565(wood, wood_hi, 0.30f));
  draw_wood_grain(cx - 2, cy - 3, 58, blend565(wood, wood_hi, 0.22f), blend565(wood, wood_dark, 0.36f));
  pm_gfx->drawCircle(cx + 1, cy + 1, 54, blend565(wood, wood_dark, 0.38f));
  draw_carved_rune(idx, cx, cy - 2, rot);
  pm_face_draw_centered_line(kSlots[slot], cy + 84, dim, 1, 1);
  pm_face_draw_centered_line(kRunes[idx].keyword, cy + 101, ink, 1, 1);
}

}  // namespace

void pm_face_runes_draw(const struct tm *tm_local, bool valid_local) {
  ensure_spread(tm_local, valid_local);
  const uint16_t bg = pm_gfx->color565(7, 8, 13);
  pm_gfx->fillScreen(bg);
  for (int r = 232; r > 150; r -= 8) {
    const float t = static_cast<float>(232 - r) / 82.f;
    pm_gfx->drawCircle(kCx, kCy, r, blend565(bg, pm_gfx->color565(88, 64, 42), 0.25f - t * 0.10f));
  }
  pm_face_draw_centered_line("RUNES", 30, pm_gfx->color565(230, 204, 138), 2, 2);
  pm_face_draw_centered_line("past  present  future", 56, pm_gfx->color565(132, 154, 164), 1, 1);
  draw_card(0, s_pose[0].x, s_pose[0].y, s_pose[0].rot);
  draw_card(1, s_pose[1].x, s_pose[1].y, s_pose[1].rot);
  draw_card(2, s_pose[2].x, s_pose[2].y, s_pose[2].rot);
  pm_face_draw_centered_line("tap to cast and hear fortune", 424, pm_gfx->color565(178, 158, 118), 1, 1);
}

void pm_face_runes_draw_voice_screen(const char *status, float thinking_progress) {
  pm_face_runes_draw(nullptr, false);
  if (thinking_progress >= 0.f) {
    pm_face_draw_thinking_progress_ring(thinking_progress);
  }
  pm_face_draw_voice_waves_overlay(thinking_progress < 0.f, millis());
  const char *label = status ? status : (thinking_progress >= 0.f ? "casting" : "speaking");
  pm_gfx->fillRect(0, 0, LCD_WIDTH, 34, pm_gfx->color565(7, 8, 13));
  pm_face_draw_centered_line(label, 12, pm_gfx->color565(230, 210, 156), 1, 1);
  pm_gfx->flush();
}

void pm_face_runes_cast(const struct tm *tm_local, bool valid_local) {
  ++s_cast_nonce;
  seed_spread(daily_seed(tm_local, valid_local) ^ mix32(millis()) ^ (s_cast_nonce * 0x6d2b79f5u));
}

bool pm_face_runes_build_fortune_message(char *out, size_t cap) {
  if (!out || cap == 0) {
    return false;
  }
  ensure_spread(nullptr, false);
  const int n = snprintf(
      out, cap,
      "Face: runes. Three selected runes for a past-present-future reading. Past: %s, keyword %s. "
      "Present: %s, keyword %s. Future: %s, keyword %s. Deliver a concise spoken fortune for this spread.",
      kRunes[s_spread[0]].name, kRunes[s_spread[0]].keyword, kRunes[s_spread[1]].name,
      kRunes[s_spread[1]].keyword, kRunes[s_spread[2]].name, kRunes[s_spread[2]].keyword);
  return n > 0 && static_cast<size_t>(n) < cap;
}

bool pm_face_runes_build_system_prompt(char *out, size_t cap) {
  if (!out || cap == 0) {
    return false;
  }
  const int n = snprintf(
      out, cap,
      "You are the Castalia Astrolabe Runes face. The watch has selected exactly three runes in a "
      "past, present, future spread. Speak a compact fortune in under 30 seconds. Use the three runes "
      "as poetic symbolic prompts, not as a claim of certain prophecy. Structure the spoken answer as "
      "Past, Present, Future, then one brief counsel. Do not mention JSON or implementation details.");
  return n > 0 && static_cast<size_t>(n) < cap;
}

const char *pm_face_runes_slot_label(int slot) {
  return (slot >= 0 && slot < kSpreadCount) ? kSlots[slot] : "";
}

const char *pm_face_runes_name(int slot) {
  if (slot < 0 || slot >= kSpreadCount) {
    return "";
  }
  return kRunes[s_spread[slot]].name;
}

const char *pm_face_runes_keyword(int slot) {
  if (slot < 0 || slot >= kSpreadCount) {
    return "";
  }
  return kRunes[s_spread[slot]].keyword;
}
