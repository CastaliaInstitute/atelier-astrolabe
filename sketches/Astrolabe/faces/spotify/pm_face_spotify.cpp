#include "faces/spotify/pm_face_spotify.h"

#include "faces/shared/pm_face_draw.h"
#include "pin_config.h"
#include "pm_display.h"
#include "pm_wifi_ntp.h"
#include <cmath>
#include <cstdio>
#include <cstring>

PmSpotifyStatus g_spotify_ui = {};

namespace {

constexpr int kCx = LCD_WIDTH / 2;
constexpr int kCy = 210;
constexpr int kRecordR = 86;
constexpr int kQueueY[] = {58, 98, 0, 318, 358};

struct StreamState {
  PmSpotifyStreamItem items[PM_SPOTIFY_STREAM_MAX];
  int item_count = 0;
  int playing_index = 0;
  int selected_index = 0;
  bool is_playing = true;
  bool hub_overlay = false;
  PmSpotifyFaceMode mode = PmSpotifyFaceMode::NowPlaying;
  uint32_t last_browse_ms = 0;
  uint32_t fake_progress_ms = 0;
  uint32_t last_progress_tick_ms = 0;
};

static StreamState s_stream;

static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

static uint16_t hash_color(const char *title, uint8_t variant) {
  uint32_t h = 2166136261u ^ variant;
  for (const char *p = title; p && *p; ++p) {
    h ^= static_cast<uint8_t>(*p);
    h *= 16777619u;
  }
  const uint8_t r = 80 + (h & 0x7F);
  const uint8_t g = 40 + ((h >> 8) & 0x5F);
  const uint8_t b = 30 + ((h >> 16) & 0x4F);
  return rgb565(r, g, b);
}

static void copy_trunc(char *dst, size_t cap, const char *src) {
  if (!dst || cap == 0) {
    return;
  }
  if (!src) {
    dst[0] = '\0';
    return;
  }
  strncpy(dst, src, cap - 1);
  dst[cap - 1] = '\0';
}

static void seed_demo_stream() {
  static const struct {
    const char *title;
    const char *artist;
  } kDemo[] = {
      {"Gymnopedie No.1", "Satie"},
      {"Clair de Lune", "Debussy"},
      {"Prelude in C", "Bach"},
      {"Vinyl Queue Demo", "Mynah"},
      {"Here Comes the Sun", "The Beatles"},
      {"Blue in Green", "Miles Davis"},
      {"Nuvole Bianche", "Einaudi"},
      {"Reverie", "Debussy"},
      {"Kind of Blue", "M. Davis"},
  };
  s_stream.item_count = static_cast<int>(sizeof(kDemo) / sizeof(kDemo[0]));
  if (s_stream.item_count > PM_SPOTIFY_STREAM_MAX) {
    s_stream.item_count = PM_SPOTIFY_STREAM_MAX;
  }
  for (int i = 0; i < s_stream.item_count; ++i) {
    copy_trunc(s_stream.items[i].title, sizeof(s_stream.items[i].title), kDemo[i].title);
    copy_trunc(s_stream.items[i].artist, sizeof(s_stream.items[i].artist), kDemo[i].artist);
    s_stream.items[i].disc_rgb565 = hash_color(kDemo[i].title, 0);
    s_stream.items[i].highlight_rgb565 = hash_color(kDemo[i].title, 1);
  }
  s_stream.playing_index = 3;
  s_stream.selected_index = 3;
  s_stream.is_playing = true;
  s_stream.mode = PmSpotifyFaceMode::NowPlaying;
  s_stream.last_browse_ms = millis();
  s_stream.fake_progress_ms = 94000;
}

static bool is_browsing(PmSpotifyFaceMode m) {
  return m == PmSpotifyFaceMode::BrowsingWhilePlaying || m == PmSpotifyFaceMode::BrowsingWhilePaused;
}

static void recompute_mode() {
  if (s_stream.item_count <= 0) {
    s_stream.mode = PmSpotifyFaceMode::NoMusic;
    return;
  }
  if (s_stream.selected_index == s_stream.playing_index) {
    s_stream.mode = s_stream.is_playing ? PmSpotifyFaceMode::NowPlaying : PmSpotifyFaceMode::Paused;
  } else {
    s_stream.mode = s_stream.is_playing ? PmSpotifyFaceMode::BrowsingWhilePlaying
                                        : PmSpotifyFaceMode::BrowsingWhilePaused;
  }
}

static int clamp_index(int idx) {
  if (s_stream.item_count <= 0) {
    return 0;
  }
  if (idx < 0) {
    return 0;
  }
  if (idx >= s_stream.item_count) {
    return s_stream.item_count - 1;
  }
  return idx;
}

static const PmSpotifyStreamItem *item_at(int idx) {
  idx = clamp_index(idx);
  return &s_stream.items[idx];
}

static void draw_queue_row(int stream_idx, int y, float opacity, bool selected) {
  const PmSpotifyStreamItem *it = item_at(stream_idx);
  if (!it || y <= 0) {
    return;
  }
  uint16_t fg = it->highlight_rgb565;
  if (opacity < 0.45f) {
    fg = pm_gfx->color565(
        static_cast<uint8_t>(40 + (it->disc_rgb565 >> 11) * 2),
        static_cast<uint8_t>(40 + ((it->disc_rgb565 >> 5) & 0x3F)),
        static_cast<uint8_t>(40 + (it->disc_rgb565 & 0x1F) * 2));
  } else if (opacity < 0.75f) {
    fg = pm_gfx->color565(180, 175, 168);
  }
  const uint8_t sz = selected ? 2 : 1;
  pm_face_draw_centered_line(it->title, y, fg, sz, sz);
  if (selected || opacity > 0.6f) {
    pm_face_draw_centered_line(it->artist, y + (selected ? 22 : 16), pm_gfx->color565(130, 135, 142), 1, 1);
  }
}

static void draw_grooves(int cx, int cy, int r, uint16_t col) {
  for (int i = 0; i < 5; ++i) {
    const int gr = r - 8 - i * 10;
    if (gr > 20) {
      pm_gfx->drawCircle(cx, cy, gr, col);
    }
  }
}

static void draw_progress_arc(int cx, int cy, int r, float progress01, uint16_t col) {
  if (progress01 <= 0.f) {
    return;
  }
  const int steps = static_cast<int>(48.f * progress01);
  const float a0 = pm_face_deg_to_rad(-90.f);
  for (int i = 0; i <= steps; ++i) {
    const float t = static_cast<float>(i) / 48.f;
    const float ang = a0 + t * pm_face_k_two_pi * progress01;
    const int x = cx + static_cast<int>(cosf(ang) * static_cast<float>(r + 6));
    const int y = cy + static_cast<int>(sinf(ang) * static_cast<float>(r + 6));
    pm_gfx->fillCircle(x, y, 2, col);
  }
}

static void draw_record(const PmSpotifyStreamItem *it, bool is_playing_track, bool selected,
                        bool show_spin, uint32_t now_ms) {
  const int cx = kCx;
  const int cy = kCy;
  const int r = kRecordR;
  const uint16_t vinyl = pm_gfx->color565(18, 18, 22);
  const uint16_t groove = pm_gfx->color565(32, 32, 38);

  pm_gfx->fillCircle(cx, cy, r + 10, pm_gfx->color565(8, 8, 10));
  pm_gfx->fillCircle(cx, cy, r, vinyl);
  draw_grooves(cx, cy, r, groove);

  const int art_r = r - 14;
  pm_gfx->fillCircle(cx, cy, art_r, it->disc_rgb565);
  pm_gfx->drawCircle(cx, cy, art_r, it->highlight_rgb565);
  pm_gfx->fillCircle(cx, cy, 6, pm_gfx->color565(12, 12, 14));
  pm_gfx->fillCircle(cx, cy, 3, pm_gfx->color565(220, 218, 210));

  if (show_spin && is_playing_track && s_stream.is_playing) {
    const float spin = static_cast<float>(now_ms % 12000u) / 12000.f * pm_face_k_two_pi;
    const int dot_x = cx + static_cast<int>(cosf(spin) * static_cast<float>(r - 4));
    const int dot_y = cy + static_cast<int>(sinf(spin) * static_cast<float>(r - 4));
    pm_gfx->fillCircle(dot_x, dot_y, 4, it->highlight_rgb565);
  }

  if (!selected) {
    return;
  }

  if (is_playing_track && s_stream.is_playing) {
    const float prog =
        fmodf(static_cast<float>(s_stream.fake_progress_ms) / 218000.f, 1.f);
    draw_progress_arc(cx, cy, r, prog, it->highlight_rgb565);
    pm_face_draw_centered_line("Now Playing", cy + r + 18, it->highlight_rgb565, 1, 1);
  } else if (is_playing_track && !s_stream.is_playing) {
    const float prog =
        fmodf(static_cast<float>(s_stream.fake_progress_ms) / 218000.f, 1.f);
    draw_progress_arc(cx, cy, r, prog, pm_gfx->color565(90, 90, 96));
    pm_gfx->fillTriangle(cx - 10, cy - 4, cx - 10, cy + 12, cx + 14, cy + 4,
                         pm_gfx->color565(240, 238, 232));
    pm_face_draw_centered_line("Paused", cy + r + 18, pm_gfx->color565(150, 155, 162), 1, 1);
  } else {
    pm_face_draw_centered_line("Tap to play", cy + r + 18, pm_gfx->color565(200, 198, 190), 1, 1);
  }
}

static void draw_now_marker() {
  if (!is_browsing(s_stream.mode)) {
    return;
  }
  const PmSpotifyStreamItem *playing = item_at(s_stream.playing_index);
  char line[56];
  snprintf(line, sizeof(line), "Now: %s", playing->title);
  pm_face_draw_centered_line(line, 400, pm_gfx->color565(120, 200, 150), 1, 1);
}

}  // namespace

void pm_face_spotify_reset() {
  seed_demo_stream();
  s_stream.hub_overlay = false;
}

void pm_face_spotify_sync_hub(const PmSpotifyStatus *hub, bool hub_ok) {
  if (!hub || s_stream.item_count <= 0) {
    return;
  }
  s_stream.hub_overlay = hub_ok && hub->ok;
  if (!s_stream.hub_overlay) {
    return;
  }
  const int pi = clamp_index(s_stream.playing_index);
  if (hub->track[0] != '\0') {
    copy_trunc(s_stream.items[pi].title, sizeof(s_stream.items[pi].title), hub->track);
  }
  if (hub->artist[0] != '\0') {
    copy_trunc(s_stream.items[pi].artist, sizeof(s_stream.items[pi].artist), hub->artist);
  }
  s_stream.is_playing = hub->is_playing;
  if (s_stream.selected_index == s_stream.playing_index) {
    recompute_mode();
  }
}

void pm_face_spotify_tick(uint32_t now_ms) {
  if (s_stream.is_playing && s_stream.selected_index == s_stream.playing_index) {
    if (s_stream.last_progress_tick_ms == 0) {
      s_stream.last_progress_tick_ms = now_ms;
    }
    const uint32_t dt = now_ms - s_stream.last_progress_tick_ms;
    s_stream.last_progress_tick_ms = now_ms;
    s_stream.fake_progress_ms += dt;
    if (s_stream.fake_progress_ms > 218000u) {
      s_stream.fake_progress_ms = 0;
    }
  }

  if (is_browsing(s_stream.mode) && s_stream.last_browse_ms != 0 &&
      now_ms - s_stream.last_browse_ms >= PM_SPOTIFY_BROWSE_TIMEOUT_MS) {
    s_stream.selected_index = s_stream.playing_index;
    recompute_mode();
    s_stream.last_browse_ms = now_ms;
  }
}

bool pm_face_spotify_needs_repaint(uint32_t now_ms) {
  (void)now_ms;
  return s_stream.is_playing && s_stream.selected_index == s_stream.playing_index;
}

void pm_face_spotify_draw() {
  if (s_stream.item_count <= 0) {
    seed_demo_stream();
  }

  const PmSpotifyStreamItem *sel = item_at(s_stream.selected_index);
  const uint16_t bg = pm_gfx->color565(
      static_cast<uint8_t>((sel->disc_rgb565 >> 11) << 3),
      static_cast<uint8_t>(((sel->disc_rgb565 >> 5) & 0x3F) << 2),
      static_cast<uint8_t>((sel->disc_rgb565 & 0x1F) << 3));
  pm_gfx->fillScreen(bg);
  pm_gfx->fillCircle(kCx, kCy, 220, pm_gfx->color565(10, 10, 14));

  if (pm_wifi_connected() && g_spotify_ui.error[0] != '\0' && !g_spotify_ui.ok) {
    char err[44];
    copy_trunc(err, sizeof(err), g_spotify_ui.error);
    pm_face_draw_centered_line(err, 28, pm_gfx->color565(255, 140, 120), 1, 1);
  } else if (!pm_wifi_connected()) {
    pm_face_draw_centered_line("Demo stream (no WiFi)", 28, pm_gfx->color565(110, 115, 122), 1, 1);
  }

  const int sel_i = s_stream.selected_index;
  for (int offset = -2; offset <= 2; ++offset) {
    if (offset == 0) {
      continue;
    }
    const int idx = clamp_index(sel_i + offset);
    const float op = (abs(offset) == 1) ? 0.7f : 0.35f;
    const int y = kQueueY[offset + 2];
    draw_queue_row(idx, y, op, false);
  }

  draw_record(sel, sel_i == s_stream.playing_index, true,
              sel_i == s_stream.playing_index, millis());
  draw_queue_row(sel_i, kCy - 52, 1.f, true);
  draw_now_marker();

  if (s_stream.hub_overlay && g_spotify_ui.device[0] != '\0') {
    char dev[40];
    snprintf(dev, sizeof(dev), "%.36s", g_spotify_ui.device);
    pm_face_draw_centered_line(dev, 430, pm_gfx->color565(100, 108, 118), 1, 1);
  }
}

bool pm_face_spotify_on_gesture(PmGestureKind kind, int16_t x, int16_t y, char *banner,
                                size_t banner_cap) {
  (void)x;
  (void)y;
  if (!banner || banner_cap == 0) {
    return false;
  }
  banner[0] = '\0';

  const uint32_t now = millis();

  if (kind == PmGestureKind::SwipeUp) {
    s_stream.selected_index = clamp_index(s_stream.selected_index + 1);
    s_stream.last_browse_ms = now;
    recompute_mode();
    snprintf(banner, banner_cap, "browse +1");
    return true;
  }
  if (kind == PmGestureKind::SwipeDown) {
    s_stream.selected_index = clamp_index(s_stream.selected_index - 1);
    s_stream.last_browse_ms = now;
    recompute_mode();
    snprintf(banner, banner_cap, "browse -1");
    return true;
  }
  if (kind == PmGestureKind::DoubleTap) {
    s_stream.selected_index = s_stream.playing_index;
    s_stream.last_browse_ms = now;
    recompute_mode();
    snprintf(banner, banner_cap, "return to now");
    return true;
  }
  if (kind == PmGestureKind::LongPress) {
    snprintf(banner, banner_cap, "menu (v2)");
    return true;
  }
  if (kind == PmGestureKind::Tap) {
    if (s_stream.selected_index == s_stream.playing_index) {
      if (pm_wifi_connected() && s_stream.hub_overlay) {
        if (s_stream.is_playing) {
          if (pm_spotify_command("stop", &g_spotify_ui)) {
            s_stream.is_playing = g_spotify_ui.is_playing;
          } else {
            s_stream.is_playing = false;
          }
          snprintf(banner, banner_cap, "pause");
        } else {
          if (pm_spotify_command("play", &g_spotify_ui)) {
            s_stream.is_playing = g_spotify_ui.is_playing;
          } else {
            s_stream.is_playing = true;
          }
          snprintf(banner, banner_cap, "play");
        }
      } else {
        s_stream.is_playing = !s_stream.is_playing;
        snprintf(banner, banner_cap, s_stream.is_playing ? "play (demo)" : "pause (demo)");
      }
      recompute_mode();
    } else {
      s_stream.playing_index = s_stream.selected_index;
      s_stream.is_playing = true;
      s_stream.last_browse_ms = now;
      recompute_mode();
      if (pm_wifi_connected() && s_stream.hub_overlay) {
        snprintf(banner, banner_cap, "play track (hub M3)");
      } else {
        snprintf(banner, banner_cap, "play (demo)");
      }
    }
    return true;
  }
  return false;
}
