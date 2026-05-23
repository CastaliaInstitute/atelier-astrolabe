#include "faces/geomancy/pm_face_geomancy.h"

#include <Arduino_GFX_Library.h>
#include <WiFi.h>
#include <cmath>
#include <cstdio>

#include <esp_system.h>

#include "faces/shared/pm_face_draw.h"
#include "pin_config.h"
#include "pm_display.h"
#include "pm_motion.h"
#include "pm_presence.h"
#include "pm_transit.h"
#include "pm_wifi_ntp.h"

namespace {

constexpr int kFigureCount = 16;
constexpr int kCx = LCD_WIDTH / 2;
constexpr int kCy = LCD_HEIGHT / 2;

struct GeomancyFigure {
  const char *title;
  const char *keyword;
  uint8_t pattern;
  uint8_t r;
  uint8_t g;
  uint8_t b;
};

const GeomancyFigure kFigures[kFigureCount] = {
    {"VIA", "road", 0b1111, 118, 198, 226},
    {"POPULUS", "many", 0b0000, 174, 184, 194},
    {"FORTUNA MAJOR", "support", 0b0011, 236, 184, 82},
    {"FORTUNA MINOR", "spark", 0b1100, 238, 146, 86},
    {"CONJUNCTIO", "meeting", 0b1001, 142, 214, 156},
    {"CARCER", "limit", 0b0110, 164, 142, 214},
    {"PUELLA", "grace", 0b0101, 232, 142, 178},
    {"PUER", "force", 0b1010, 232, 102, 82},
    {"ALBUS", "clear", 0b0010, 218, 226, 220},
    {"RUBEUS", "heat", 0b1101, 224, 78, 86},
    {"ACQUISITIO", "gain", 0b0111, 106, 210, 154},
    {"AMISSIO", "release", 0b1110, 110, 174, 224},
    {"LAETITIA", "rise", 0b0001, 244, 204, 94},
    {"TRISTITIA", "descent", 0b1000, 128, 150, 184},
    {"CAPUT DRACONIS", "threshold", 0b1011, 126, 218, 202},
    {"CAUDA DRACONIS", "ending", 0b0100, 202, 126, 218},
};

int s_selected = -1;
int s_last_drawn = 0;
uint32_t s_cast_seed = 0;

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

uint32_t mix32(uint32_t x) {
  x ^= x >> 16;
  x *= 0x7feb352du;
  x ^= x >> 15;
  x *= 0x846ca68bu;
  x ^= x >> 16;
  return x;
}

void draw_dot(int x, int y, int r, uint16_t fill, uint16_t rim) {
  pm_gfx->fillCircle(x, y, r, fill);
  pm_gfx->drawCircle(x, y, r, rim);
  pm_gfx->drawCircle(x, y, r + 1, rim);
}

void draw_figure(int cx, int cy, const GeomancyFigure &fig, uint16_t accent, uint16_t dim) {
  constexpr int kRowGap = 42;
  constexpr int kDotR = 12;
  for (int row = 0; row < 4; ++row) {
    const bool single = ((fig.pattern >> (3 - row)) & 0x01) != 0;
    const int y = cy - 63 + row * kRowGap;
    if (single) {
      draw_dot(cx, y, kDotR, accent, pm_gfx->color565(255, 242, 210));
    } else {
      draw_dot(cx - 27, y, kDotR, dim, accent);
      draw_dot(cx + 27, y, kDotR, dim, accent);
    }
  }
}

void draw_witness(int idx, int slot, int x, int y, uint16_t bg) {
  const GeomancyFigure &fig = kFigures[idx % kFigureCount];
  const uint16_t accent = pm_gfx->color565(fig.r, fig.g, fig.b);
  const uint16_t dim = blend565(bg, accent, 0.34f);
  pm_gfx->drawRect(x - 32, y - 48, 64, 96, dim);
  for (int row = 0; row < 4; ++row) {
    const bool single = ((fig.pattern >> (3 - row)) & 0x01) != 0;
    const int yy = y - 27 + row * 18;
    if (single) {
      pm_gfx->fillCircle(x, yy, 5, accent);
    } else {
      pm_gfx->fillCircle(x - 10, yy, 5, accent);
      pm_gfx->fillCircle(x + 10, yy, 5, accent);
    }
  }
  char label[4];
  snprintf(label, sizeof(label), "%d", slot + 1);
  pm_face_draw_centered_line(label, y + 58, dim, 1, 1);
}

}  // namespace

int pm_face_geomancy_index(const struct tm *tm_local, bool valid_local) {
  if (s_selected >= 0) return s_selected;
  if (!valid_local || !tm_local) {
    return static_cast<int>((millis() / 60000u) % kFigureCount);
  }
  const uint32_t seed = static_cast<uint32_t>((tm_local->tm_year + 1900) * 1009 +
                                             (tm_local->tm_yday + 1) * 37 + tm_local->tm_mday);
  return static_cast<int>(mix32(seed) % kFigureCount);
}

bool pm_face_geomancy_cycle(int delta) {
  const int base = s_selected >= 0 ? s_selected : s_last_drawn;
  s_selected = (base + delta + kFigureCount) % kFigureCount;
  s_cast_seed = 0;
  return true;
}

void pm_face_geomancy_cast_entropy(int16_t touch_x, int16_t touch_y) {
  uint32_t seed = mix32(esp_random() ^ micros() ^ (millis() << 11));
  seed ^= mix32((static_cast<uint32_t>(static_cast<uint16_t>(touch_x)) << 16) |
                static_cast<uint16_t>(touch_y));

  float ax = 0.f;
  float ay = 0.f;
  float az = 0.f;
  if (pm_motion_accel_g(&ax, &ay, &az)) {
    seed ^= mix32(static_cast<uint32_t>(lrintf((ax + 8.f) * 1000.f)) ^
                  (static_cast<uint32_t>(lrintf((ay + 8.f) * 1000.f)) << 10) ^
                  (static_cast<uint32_t>(lrintf((az + 8.f) * 1000.f)) << 20));
  }
  seed ^= mix32(static_cast<uint32_t>(lrintf(pm_motion_yaw_deg() * 100.f)));

  if (pm_wifi_connected()) {
    seed ^= mix32(static_cast<uint32_t>(WiFi.RSSI() + 160) * 0x45d9f3bu);
  }

  const size_t peer_count = pm_presence_peer_count();
  seed ^= mix32(static_cast<uint32_t>(peer_count) * 0x9e3779b9u);
  for (size_t i = 0; i < peer_count && i < 4; ++i) {
    const PmPresencePeer *peer = pm_presence_peer(i);
    if (peer) {
      seed ^= mix32(peer->device_id ^ (static_cast<uint32_t>(peer->last_seen_ms) << (i + 1)) ^
                    (static_cast<uint32_t>(static_cast<uint8_t>(peer->rssi_dbm)) << 24));
    }
  }

  if (pm_time_valid()) {
    struct tm utc = {};
    pm_time_utc(&utc);
    PmTransitPositions pos = {};
    pm_transit_compute_utc(&utc, &pos);
    if (pos.ok) {
      double phase = pos.lon[kPmBodyMoon] - pos.lon[kPmBodySun];
      while (phase < 0.0) phase += 360.0;
      while (phase >= 360.0) phase -= 360.0;
      seed ^= mix32(static_cast<uint32_t>(phase * 1000.0));
    }
  }

  s_cast_seed = seed;
  s_selected = static_cast<int>(mix32(seed) % kFigureCount);
}

void pm_face_geomancy_reset_daily(void) {
  s_selected = -1;
  s_cast_seed = 0;
}

const char *pm_face_geomancy_title(int idx) {
  if (idx < 0) idx = 0;
  return kFigures[idx % kFigureCount].title;
}

const char *pm_face_geomancy_keyword(int idx) {
  if (idx < 0) idx = 0;
  return kFigures[idx % kFigureCount].keyword;
}

void pm_face_geomancy_draw(const struct tm *tm_local, bool valid_local) {
  const int idx = pm_face_geomancy_index(tm_local, valid_local);
  s_last_drawn = idx;
  const GeomancyFigure &fig = kFigures[idx];
  const uint16_t bg = pm_gfx->color565(9, 13, 12);
  const uint16_t accent = pm_gfx->color565(fig.r, fig.g, fig.b);
  const uint16_t dim = blend565(bg, accent, 0.42f);
  const uint16_t text = pm_gfx->color565(232, 232, 218);
  const bool cast = s_selected >= 0 && s_cast_seed != 0;
  pm_gfx->fillScreen(bg);

  for (int i = 0; i < 16; ++i) {
    const float a = -pm_face_k_pi * 0.5f + static_cast<float>(i) * pm_face_k_two_pi / 16.f;
    const int x0 = kCx + static_cast<int>(lrintf(cosf(a) * 204.f));
    const int y0 = kCy + static_cast<int>(lrintf(sinf(a) * 204.f));
    const int x1 = kCx + static_cast<int>(lrintf(cosf(a) * 222.f));
    const int y1 = kCy + static_cast<int>(lrintf(sinf(a) * 222.f));
    const uint16_t tick = i == idx ? accent : pm_gfx->color565(44, 54, 52);
    pm_gfx->drawLine(x0, y0, x1, y1, tick);
    if ((i % 4) == 0) {
      pm_gfx->drawLine((x0 + kCx) / 2, (y0 + kCy) / 2, x1, y1, blend565(bg, tick, 0.52f));
    }
  }
  pm_gfx->drawCircle(kCx, kCy, 224, dim);
  pm_gfx->drawCircle(kCx, kCy, 180, pm_gfx->color565(38, 48, 46));

  draw_witness((idx + 5) % kFigureCount, 0, 88, 122, bg);
  draw_witness((idx + 9) % kFigureCount, 1, 392, 122, bg);
  draw_witness((idx + 12) % kFigureCount, 2, 88, 344, bg);
  draw_witness((idx + 3) % kFigureCount, 3, 392, 344, bg);

  pm_gfx->fillCircle(kCx, kCy, 105, pm_gfx->color565(16, 22, 20));
  pm_gfx->drawCircle(kCx, kCy, 105, accent);
  pm_gfx->drawCircle(kCx, kCy, 112, dim);
  draw_figure(kCx, kCy - 2, fig, accent, pm_gfx->color565(58, 68, 64));

  pm_face_draw_centered_line("GEOMANCY", 34, accent, 2, 2);
  pm_face_draw_centered_line(fig.title, 356, text, 2, 2);
  pm_face_draw_centered_line(fig.keyword, 386, blend565(bg, accent, 0.80f), 1, 1);
  pm_face_draw_centered_line(cast ? "entropy cast" : (s_selected >= 0 ? "swipe figures" : "daily figure"), 420,
                             pm_gfx->color565(150, 162, 154), 1, 1);
}
