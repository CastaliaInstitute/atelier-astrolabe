#include "faces/cauldron/pm_face_cauldron.h"

#include <Arduino_GFX_Library.h>
#include <cmath>

#include "faces/shared/pm_face_draw.h"
#include "pin_config.h"
#include "pm_display.h"
#include "pm_touch.h"

namespace {

constexpr int kCx = LCD_WIDTH / 2;
constexpr int kCy = LCD_HEIGHT / 2;
constexpr int kMinDim = LCD_WIDTH < LCD_HEIGHT ? LCD_WIDTH : LCD_HEIGHT;
constexpr int kBowlR = kMinDim / 2 - 18;
constexpr int kLiquidR = kBowlR - 26;
constexpr int kMistN = 42;
constexpr int kBubbleN = 18;

struct Mist {
  float a;
  float r;
  float z;
  float vr;
  float va;
  uint8_t hue;
};

struct Bubble {
  float x;
  float y;
  float vy;
  float r;
  float life;
};

Mist s_mist[kMistN];
Bubble s_bubbles[kBubbleN];
uint32_t s_last_ms = 0;
uint32_t s_last_paint_ms = 0;
float s_spin = 0.45f;
float s_heat = 0.65f;
float s_stir = 0.f;
bool s_seeded = false;
bool s_touching = false;
int16_t s_last_x = 0;
int16_t s_last_y = 0;

float frand(uint32_t seed, float lo, float hi) {
  seed ^= seed << 13;
  seed ^= seed >> 17;
  seed ^= seed << 5;
  const float t = static_cast<float>(seed & 0xffffu) / 65535.f;
  return lo + (hi - lo) * t;
}

uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return pm_gfx->color565(r, g, b);
}

uint16_t vapor_color(uint8_t hue, float k) {
  k = fmaxf(0.f, fminf(1.f, k));
  const uint8_t r = static_cast<uint8_t>((18 + hue / 8) * k);
  const uint8_t g = static_cast<uint8_t>((70 + hue / 4) * k);
  const uint8_t b = static_cast<uint8_t>((92 + hue / 2) * k);
  return rgb(r, g, b);
}

void seed_particles(void) {
  for (int i = 0; i < kMistN; ++i) {
    s_mist[i].a = frand(i * 97u + 11u, 0.f, pm_face_k_two_pi);
    s_mist[i].r = frand(i * 131u + 29u, 4.f, static_cast<float>(kLiquidR - 6));
    s_mist[i].z = frand(i * 173u + 41u, 0.f, 1.f);
    s_mist[i].vr = frand(i * 191u + 7u, -7.f, 9.f);
    s_mist[i].va = frand(i * 223u + 19u, -0.42f, 0.74f);
    s_mist[i].hue = static_cast<uint8_t>(frand(i * 251u + 3u, 0.f, 120.f));
  }
  for (int i = 0; i < kBubbleN; ++i) {
    const float a = frand(i * 59u + 5u, 0.f, pm_face_k_two_pi);
    const float r = frand(i * 83u + 13u, 8.f, static_cast<float>(kLiquidR - 18));
    s_bubbles[i].x = cosf(a) * r;
    s_bubbles[i].y = sinf(a) * r;
    s_bubbles[i].vy = frand(i * 109u + 23u, 9.f, 24.f);
    s_bubbles[i].r = frand(i * 137u + 31u, 2.5f, 8.5f);
    s_bubbles[i].life = frand(i * 149u + 37u, 0.15f, 1.f);
  }
  s_seeded = true;
}

bool in_bowl(int x, int y) {
  const float dx = static_cast<float>(x - kCx);
  const float dy = static_cast<float>(y - kCy);
  return dx * dx + dy * dy <= static_cast<float>(kLiquidR * kLiquidR);
}

void draw_disc_blob(int x, int y, int r, uint16_t c0, uint16_t c1) {
  if (r <= 0) {
    return;
  }
  pm_gfx->fillCircle(x, y, r, c0);
  if (r > 5) {
    pm_gfx->fillCircle(x - r / 4, y - r / 5, r / 2, c1);
  }
}

void draw_cauldron(void) {
  pm_gfx->fillScreen(0x0000);

  const uint16_t iron_outer = rgb(5, 6, 8);
  const uint16_t iron_mid = rgb(22, 24, 29);
  const uint16_t iron_hi = rgb(62, 66, 74);
  const uint16_t liquid0 = rgb(2, 13, 13);
  const uint16_t liquid1 = rgb(4, 28, 25);
  const uint16_t glow = rgb(50, 220, 170);

  pm_gfx->fillCircle(kCx, kCy, kBowlR + 8, rgb(1, 1, 2));
  pm_gfx->fillCircle(kCx, kCy, kBowlR, iron_outer);
  pm_gfx->fillCircle(kCx, kCy, kBowlR - 7, iron_mid);
  pm_gfx->drawCircle(kCx, kCy, kBowlR - 2, iron_hi);
  pm_gfx->drawCircle(kCx, kCy, kBowlR - 12, rgb(34, 38, 44));
  pm_gfx->fillCircle(kCx, kCy, kLiquidR + 8, rgb(3, 7, 8));
  pm_gfx->fillCircle(kCx, kCy, kLiquidR, liquid0);

  for (int r = kLiquidR - 10; r > 18; r -= 18) {
    const uint8_t c = static_cast<uint8_t>(18 + (kLiquidR - r) / 4);
    pm_gfx->drawCircle(kCx, kCy, r, rgb(2, c, c + 6));
  }
  for (int i = 0; i < 20; ++i) {
    const float a = static_cast<float>(i) * pm_face_k_two_pi / 20.f + s_spin * 0.25f;
    const int r0 = 22 + (i * 31) % (kLiquidR - 34);
    const int x = kCx + static_cast<int>(cosf(a) * static_cast<float>(r0));
    const int y = kCy + static_cast<int>(sinf(a) * static_cast<float>(r0));
    pm_gfx->drawPixel(x, y, (i % 4 == 0) ? glow : liquid1);
  }
  pm_gfx->drawCircle(kCx, kCy, kLiquidR, glow);
}

void step(float dt) {
  if (!s_seeded) {
    seed_particles();
  }
  s_heat += (0.58f + fabsf(s_spin) * 0.22f + s_stir * 0.25f - s_heat) * fminf(1.f, dt * 1.8f);
  s_stir *= powf(0.025f, dt);
  s_spin += (0.42f - s_spin) * fminf(1.f, dt * 0.35f);

  for (int i = 0; i < kMistN; ++i) {
    Mist &m = s_mist[i];
    m.a += (m.va + s_spin * (1.15f + m.z)) * dt;
    m.r += (m.vr + s_stir * 16.f * sinf(m.a * 1.7f)) * dt;
    m.z += (0.12f + s_heat * 0.12f) * dt;
    if (m.r < 3.f || m.r > static_cast<float>(kLiquidR - 4) || m.z > 1.25f) {
      m.a = frand(m.hue * 31u + millis(), 0.f, pm_face_k_two_pi);
      m.r = frand(m.hue * 43u + millis(), 8.f, 60.f);
      m.z = 0.f;
      m.vr = frand(m.hue * 47u + millis(), -3.f, 11.f);
    }
  }

  for (int i = 0; i < kBubbleN; ++i) {
    Bubble &b = s_bubbles[i];
    const float a = atan2f(b.y, b.x);
    const float r = hypotf(b.x, b.y);
    const float next_a = a + (0.4f + s_spin * 0.55f) * dt;
    const float next_r = r + sinf(next_a * 3.1f + s_spin * 2.f) * dt * (5.f + s_stir * 14.f);
    b.x = cosf(next_a) * next_r;
    b.y = sinf(next_a) * next_r;
    b.life -= dt * 0.18f;
    if (b.life <= 0.f || next_r > static_cast<float>(kLiquidR - 8) || next_r < 6.f) {
      const float na = frand(i * 197u + millis(), 0.f, pm_face_k_two_pi);
      const float nr = frand(i * 211u + millis(), 10.f, static_cast<float>(kLiquidR - 22));
      b.x = cosf(na) * nr;
      b.y = sinf(na) * nr;
      b.vy = frand(i * 229u + millis(), 10.f, 28.f);
      b.r = frand(i * 239u + millis(), 2.5f, 8.5f);
      b.life = 1.f;
    }
  }
}

void draw_vapor(void) {
  const uint32_t now = millis();
  for (int i = 0; i < kMistN; ++i) {
    const Mist &m = s_mist[i];
    const float wobble = sinf(m.a * 2.2f + static_cast<float>(now) * 0.0017f);
    const float rr = m.r * (1.0f + 0.18f * wobble);
    const int x = kCx + static_cast<int>(cosf(m.a) * rr);
    const int y = kCy + static_cast<int>(sinf(m.a) * rr);
    const int r = static_cast<int>((7.f + 18.f * (1.f - m.z)) * (0.68f + 0.38f * s_heat));
    const float edge = fminf(1.f, rr / static_cast<float>(kLiquidR));
    const float shade = (1.f - m.z) * (0.32f + 0.44f * s_heat) * (1.f - edge * 0.35f);
    draw_disc_blob(x, y, r, vapor_color(m.hue, shade), vapor_color(static_cast<uint8_t>(m.hue + 50), shade * 0.55f));
  }

  for (int i = 0; i < kBubbleN; ++i) {
    const Bubble &b = s_bubbles[i];
    const int x = kCx + static_cast<int>(b.x);
    const int y = kCy + static_cast<int>(b.y);
    if (!in_bowl(x, y)) {
      continue;
    }
    const int r = static_cast<int>(b.r * (0.8f + 0.6f * (1.f - b.life)));
    const uint16_t c = vapor_color(static_cast<uint8_t>(60 + i * 7), 0.42f + 0.28f * s_heat);
    pm_gfx->drawCircle(x, y, r, c);
    if (r > 3) {
      pm_gfx->drawPixel(x - r / 3, y - r / 3, rgb(180, 255, 230));
    }
  }
}

}  // namespace

void pm_face_cauldron_on_enter(void) {
  seed_particles();
  s_last_ms = 0;
  s_last_paint_ms = 0;
  s_stir = 0.f;
  s_touching = false;
}

void pm_face_cauldron_on_leave(void) {
  s_touching = false;
  s_stir = 0.f;
}

void pm_face_cauldron_draw(void) {
  draw_cauldron();
  draw_vapor();
}

bool pm_face_cauldron_anim_tick(uint32_t now_ms) {
  if (s_last_ms == 0) {
    s_last_ms = now_ms;
    return true;
  }
  const float dt = fminf(0.08f, static_cast<float>(now_ms - s_last_ms) * 0.001f);
  s_last_ms = now_ms;
  step(dt);
  if (now_ms - s_last_paint_ms < 48u) {
    return false;
  }
  s_last_paint_ms = now_ms;
  return true;
}

bool pm_face_cauldron_touch_tick(uint32_t now_ms) {
  (void)now_ms;
  int16_t xs[1];
  int16_t ys[1];
  const uint8_t n = pm_touch_sample(xs, ys, 1);
  if (n == 0) {
    s_touching = false;
    return false;
  }
  const int16_t x = xs[0];
  const int16_t y = ys[0];
  if (s_touching) {
    const float dx = static_cast<float>(x - s_last_x);
    const float dy = static_cast<float>(y - s_last_y);
    const float lever = fmaxf(24.f, hypotf(static_cast<float>(x - kCx), static_cast<float>(y - kCy)));
    const float tangent = (static_cast<float>(x - kCx) * dy - static_cast<float>(y - kCy) * dx) / (lever * 42.f);
    s_spin += tangent;
    s_spin = fmaxf(-3.2f, fminf(3.2f, s_spin));
    s_stir = fminf(1.8f, s_stir + hypotf(dx, dy) * 0.016f);
  } else {
    s_stir = fmaxf(s_stir, 0.45f);
  }
  s_last_x = x;
  s_last_y = y;
  s_touching = true;
  return true;
}
