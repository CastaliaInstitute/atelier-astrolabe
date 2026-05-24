#include "faces/alethiometer/pm_face_alethiometer.h"

#include <Arduino_GFX_Library.h>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "faces/alethiometer/pm_alethiometer_emoji_glyphs.h"
#include "faces/shared/pm_face_draw.h"
#include "pin_config.h"
#include "pm_display.h"

namespace {

constexpr int kSymbolCount = 36;
constexpr int kNeedleCount = 4;
constexpr int kCx = LCD_WIDTH / 2;
constexpr int kCy = LCD_HEIGHT / 2;

struct Symbol {
  const char *name;
};

const Symbol kSymbols[kSymbolCount] = {
    {"alpha"},     {"bee"},    {"sun"},      {"moon"},     {"hourglass"}, {"key"},
    {"anchor"},    {"heart"},  {"crown"},    {"sword"},    {"tree"},      {"serpent"},
    {"bridge"},    {"lantern"}, {"book"},     {"mask"},     {"ship"},      {"thunder"},
    {"eye"},       {"cloud"},  {"mountain"}, {"road"},     {"cup"},       {"butterfly"},
    {"well"},      {"mirror"}, {"scales"},   {"fire"},     {"feather"},   {"gate"},
    {"star"},      {"wheel"},  {"hand"},     {"lyre"},     {"arrow"},     {"omega"},
};

float s_angle[kNeedleCount] = {-pm_face_k_pi * 0.5f, -0.2f, 1.3f, 2.6f};
float s_target[kNeedleCount] = {-pm_face_k_pi * 0.5f, -0.2f, 1.3f, 2.6f};
uint32_t s_last_anim_ms = 0;
uint32_t s_last_seed_ms = 0;
bool s_animating = true;

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

uint32_t hash_text(const char *text, uint32_t seed) {
  uint32_t h = 2166136261u ^ seed;
  if (!text) {
    return h;
  }
  while (*text) {
    h ^= static_cast<uint8_t>(*text++);
    h *= 16777619u;
  }
  return h;
}

float symbol_angle(int idx) {
  return -pm_face_k_pi * 0.5f + static_cast<float>(idx) * pm_face_k_two_pi / static_cast<float>(kSymbolCount);
}

float norm_angle(float a) {
  while (a < -pm_face_k_pi) {
    a += pm_face_k_two_pi;
  }
  while (a > pm_face_k_pi) {
    a -= pm_face_k_two_pi;
  }
  return a;
}

void set_target_symbol(int needle, int symbol_idx) {
  if (needle < 0 || needle >= kNeedleCount) {
    return;
  }
  symbol_idx %= kSymbolCount;
  if (symbol_idx < 0) {
    symbol_idx += kSymbolCount;
  }
  s_target[needle] = symbol_angle(symbol_idx);
}

int target_symbol(int needle) {
  float a = s_target[needle] + pm_face_k_pi * 0.5f;
  while (a < 0.f) {
    a += pm_face_k_two_pi;
  }
  while (a >= pm_face_k_two_pi) {
    a -= pm_face_k_two_pi;
  }
  return static_cast<int>(lrintf(a * static_cast<float>(kSymbolCount) / pm_face_k_two_pi)) % kSymbolCount;
}

void seed_idle_targets(uint32_t now_ms) {
  if (now_ms - s_last_seed_ms < 9000u) {
    return;
  }
  s_last_seed_ms = now_ms;
  const uint32_t h = now_ms / 1000u;
  set_target_symbol(0, static_cast<int>((h * 5u + 3u) % kSymbolCount));
  set_target_symbol(1, static_cast<int>((h * 7u + 11u) % kSymbolCount));
  set_target_symbol(2, static_cast<int>((h * 13u + 19u) % kSymbolCount));
  set_target_symbol(3, static_cast<int>((h * 17u + 29u) % kSymbolCount));
  s_animating = true;
}

void draw_needle(float angle, int len, uint16_t col, int half_w, bool long_answer) {
  pm_face_draw_hand_radial(kCx, kCy, angle, len, col, half_w);
  const int tip_x = kCx + static_cast<int>(lrintf(cosf(angle) * static_cast<float>(len)));
  const int tip_y = kCy + static_cast<int>(lrintf(sinf(angle) * static_cast<float>(len)));
  pm_gfx->fillCircle(tip_x, tip_y, long_answer ? 5 : 4, col);
  pm_gfx->drawLine(kCx, kCy, kCx - static_cast<int>(lrintf(cosf(angle) * static_cast<float>(pm_face_scale_i(24)))),
                   kCy - static_cast<int>(lrintf(sinf(angle) * static_cast<float>(pm_face_scale_i(24)))),
                   blend565(pm_gfx->color565(12, 9, 16), col, 0.65f));
}

void draw_symbol_ring(void) {
  const uint16_t gold = pm_gfx->color565(214, 172, 84);
  const uint16_t dim = pm_gfx->color565(96, 76, 54);
  const uint16_t ink = pm_gfx->color565(235, 220, 164);
  const uint16_t answer = pm_gfx->color565(132, 198, 238);
  pm_gfx->drawCircle(kCx, kCy, pm_face_scale_i(213), gold);
  pm_gfx->drawCircle(kCx, kCy, pm_face_scale_i(198), dim);
  pm_gfx->drawCircle(kCx, kCy, pm_face_scale_i(150), dim);
  const int q0 = target_symbol(0);
  const int q1 = target_symbol(1);
  const int q2 = target_symbol(2);
  const int ans = target_symbol(3);
  for (int i = 0; i < kSymbolCount; ++i) {
    const float a = symbol_angle(i);
    const int r0 = pm_face_scale_i((i % 3 == 0) ? 188 : 194);
    const int x0 = kCx + static_cast<int>(lrintf(cosf(a) * static_cast<float>(r0)));
    const int y0 = kCy + static_cast<int>(lrintf(sinf(a) * static_cast<float>(r0)));
    const int x1 = kCx + static_cast<int>(lrintf(cosf(a) * static_cast<float>(pm_face_scale_i(210))));
    const int y1 = kCy + static_cast<int>(lrintf(sinf(a) * static_cast<float>(pm_face_scale_i(210))));
    pm_gfx->drawLine(x0, y0, x1, y1, (i % 3 == 0) ? gold : dim);
    const int tx = kCx + static_cast<int>(lrintf(cosf(a) * static_cast<float>(pm_face_scale_i(174))));
    const int ty = kCy + static_cast<int>(lrintf(sinf(a) * static_cast<float>(pm_face_scale_i(174))));
    const bool question_hit = i == q0 || i == q1 || i == q2;
    const uint16_t icon = i == ans ? answer : (question_hit ? pm_gfx->color565(255, 232, 170) : ink);
    if (question_hit || i == ans) {
      pm_gfx->fillCircle(tx, ty, pm_face_scale_i(14), blend565(pm_gfx->color565(8, 7, 12), icon, 0.20f));
      pm_gfx->drawCircle(tx, ty, pm_face_scale_i(14), icon);
    }
    pm_alethiometer_draw_emoji_glyph(pm_gfx, tx, ty, i, icon);
  }
}

void draw_center_orrery(void) {
  const uint16_t base = pm_gfx->color565(26, 18, 28);
  const uint16_t gold = pm_gfx->color565(230, 184, 88);
  const uint16_t blue = pm_gfx->color565(88, 142, 188);
  pm_gfx->fillCircle(kCx, kCy, pm_face_scale_i(92), base);
  pm_gfx->drawCircle(kCx, kCy, pm_face_scale_i(92), pm_gfx->color565(128, 92, 50));
  pm_gfx->drawCircle(kCx, kCy, pm_face_scale_i(70), pm_gfx->color565(70, 54, 62));
  pm_gfx->drawCircle(kCx, kCy, pm_face_scale_i(48), pm_gfx->color565(78, 86, 98));
  for (int i = 0; i < 12; ++i) {
    const float a = -pm_face_k_pi * 0.5f + static_cast<float>(i) * pm_face_k_two_pi / 12.f;
    pm_gfx->drawLine(kCx, kCy, kCx + static_cast<int>(lrintf(cosf(a) * static_cast<float>(pm_face_scale_i(86)))),
                     kCy + static_cast<int>(lrintf(sinf(a) * static_cast<float>(pm_face_scale_i(86)))),
                     blend565(base, (i % 3 == 0) ? gold : blue, 0.45f));
  }
  pm_gfx->fillCircle(kCx, kCy, pm_face_scale_i(17), pm_gfx->color565(20, 14, 20));
  pm_gfx->drawCircle(kCx, kCy, pm_face_scale_i(17), gold);
  pm_gfx->fillCircle(kCx, kCy, pm_face_scale_i(5), gold);
}

void draw_readout(void) {
  const uint16_t txt = pm_gfx->color565(222, 206, 158);
  char line[58];
  snprintf(line, sizeof(line), "%s  %s  %s",
           pm_face_alethiometer_symbol_name(target_symbol(0)),
           pm_face_alethiometer_symbol_name(target_symbol(1)),
           pm_face_alethiometer_symbol_name(target_symbol(2)));
  pm_face_draw_centered_line(line, pm_face_scale_y(382), txt, 1, 1);
  snprintf(line, sizeof(line), "answer: %s", pm_face_alethiometer_symbol_name(target_symbol(3)));
  pm_face_draw_centered_line(line, pm_face_scale_y(402), pm_gfx->color565(150, 194, 226), 1, 1);
}

}  // namespace

void pm_face_alethiometer_draw(void) {
  pm_gfx->fillScreen(pm_gfx->color565(8, 7, 12));
  for (int r = pm_face_scale_i(228); r > pm_face_scale_i(184); r -= pm_face_scale_i(4)) {
    const float t = static_cast<float>(pm_face_scale_i(228) - r) / static_cast<float>(pm_face_scale_i(44));
    const uint16_t col = blend565(pm_gfx->color565(10, 8, 14), pm_gfx->color565(112, 76, 34), 0.16f + t * 0.10f);
    pm_gfx->drawCircle(kCx, kCy, r, col);
  }
  draw_symbol_ring();
  draw_center_orrery();
  draw_needle(s_angle[0], pm_face_scale_i(132), pm_gfx->color565(225, 182, 92), 2, false);
  draw_needle(s_angle[1], pm_face_scale_i(122), pm_gfx->color565(196, 146, 80), 2, false);
  draw_needle(s_angle[2], pm_face_scale_i(112), pm_gfx->color565(178, 128, 70), 2, false);
  draw_needle(s_angle[3], pm_face_scale_i(184), pm_gfx->color565(102, 178, 230), 2, true);
  pm_gfx->fillCircle(kCx, kCy, pm_face_scale_i(9), pm_gfx->color565(238, 198, 96));
  pm_gfx->drawCircle(kCx, kCy, pm_face_scale_i(13), pm_gfx->color565(70, 48, 34));
  pm_face_draw_centered_line("ALETHIOMETER", pm_face_scale_y(44), pm_gfx->color565(228, 198, 128), 1, 1);
  draw_readout();
}

void pm_face_alethiometer_draw_voice_screen(const char *status, float thinking_progress) {
  pm_face_alethiometer_draw();
  if (thinking_progress >= 0.f) {
    pm_face_draw_thinking_progress_ring(thinking_progress);
  }
  pm_face_draw_voice_waves_overlay(thinking_progress < 0.f, millis());
  const char *label = status ? status : (thinking_progress >= 0.f ? "consulting" : "speaking");
  pm_gfx->fillRect(0, 0, LCD_WIDTH, 32, pm_gfx->color565(8, 7, 12));
  pm_face_draw_centered_line(label, 12, pm_gfx->color565(230, 210, 156), 1, 1);
  pm_gfx->flush();
}

bool pm_face_alethiometer_anim_tick(uint32_t now_ms) {
  seed_idle_targets(now_ms);
  if (s_last_anim_ms == 0) {
    s_last_anim_ms = now_ms;
  }
  const float dt = static_cast<float>(now_ms - s_last_anim_ms) / 1000.f;
  s_last_anim_ms = now_ms;
  bool moving = false;
  for (int i = 0; i < kNeedleCount; ++i) {
    const float d = norm_angle(s_target[i] - s_angle[i]);
    if (fabsf(d) > 0.006f) {
      const float speed = (i == 3) ? 3.1f : 2.2f;
      float step = d * (1.f - expf(-speed * dt));
      if (fabsf(step) < 0.004f) {
        step = d > 0.f ? 0.004f : -0.004f;
      }
      if (fabsf(step) > fabsf(d)) {
        step = d;
      }
      s_angle[i] = norm_angle(s_angle[i] + step);
      moving = true;
    }
  }
  const bool repaint = moving || s_animating;
  s_animating = moving;
  return repaint;
}

void pm_face_alethiometer_seed_from_text(const char *question, const char *reply) {
  const uint32_t qh = hash_text(question, 0x44a1e7u);
  const uint32_t rh = hash_text(reply, 0xa11e7u);
  set_target_symbol(0, static_cast<int>((qh >> 1) % kSymbolCount));
  set_target_symbol(1, static_cast<int>((qh >> 9) % kSymbolCount));
  set_target_symbol(2, static_cast<int>((qh >> 17) % kSymbolCount));
  set_target_symbol(3, static_cast<int>((rh ^ (qh >> 5)) % kSymbolCount));
  s_last_seed_ms = millis();
  s_animating = true;
}

bool pm_face_alethiometer_build_system_prompt(char *out, size_t cap) {
  if (!out || cap == 0) {
    return false;
  }
  const int n = snprintf(
      out, cap,
      "You are the Castalia Astrolabe Alethiometer face. The user will ask one spoken question. "
      "Internally choose symbols from this 36-symbol ring: alpha, bee, sun, moon, hourglass, key, "
      "anchor, heart, crown, sword, tree, serpent, bridge, lantern, book, mask, ship, thunder, eye, "
      "cloud, mountain, road, cup, butterfly, well, mirror, scales, fire, feather, gate, star, wheel, "
      "hand, lyre, arrow, omega. Use this machine contract internally: "
      "{\"question\":\"...\",\"needles\":[{\"symbol\":\"...\",\"meaning\":\"...\"},{\"symbol\":\"...\","
      "\"meaning\":\"...\"},{\"symbol\":\"...\",\"meaning\":\"...\"}],\"answer_needle\":{\"symbol\":\"...\","
      "\"meaning\":\"...\"},\"interpretation\":\"...\"}. The three short needles phrase the question; "
      "the long needle indicates the answer. Speak only the interpretation in under 35 seconds. "
      "Sound like an experimental narrative compass, not a real divination authority. Do not read JSON aloud.");
  return n > 0 && static_cast<size_t>(n) < cap;
}

const char *pm_face_alethiometer_symbol_name(int idx) {
  if (idx < 0) {
    idx = 0;
  }
  return kSymbols[idx % kSymbolCount].name;
}

const char *pm_face_alethiometer_needle_symbol_name(int needle) {
  if (needle < 0 || needle >= kNeedleCount) {
    return "";
  }
  return pm_face_alethiometer_symbol_name(target_symbol(needle));
}
