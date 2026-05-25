#include "faces/spotify/pm_face_spotify.h"

#include <HTTPClient.h>
#include <JPEGDEC.h>
#include <WiFiClientSecure.h>
#include "faces/shared/pm_face_draw.h"
#include "pin_config.h"
#include "pm_config.h"
#include "pm_display.h"
#include "pm_heap.h"
#include "pm_wifi_ntp.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>

PmSpotifyStatus g_spotify_ui = {};

namespace {

constexpr int kCx = LCD_WIDTH / 2;
constexpr int kCy = LCD_HEIGHT / 2;
constexpr int kMinDim = LCD_WIDTH < LCD_HEIGHT ? LCD_WIDTH : LCD_HEIGHT;
constexpr int kRecordR = kMinDim / 2 - 22;
constexpr int kAlbumR = kRecordR / 3;
constexpr int kQueueY[] = {58, 98, 0, 318, 358};
constexpr int kAlbumArtSize = 64;
constexpr int kAlbumArtMaxBytes = 140000;
constexpr uint32_t kAlbumArtMinFetchHeap = 48000u;

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
static uint16_t *s_album_art_fb = nullptr;
static char s_album_art_url[192] = "";
static bool s_album_art_ready = false;
static bool s_album_art_fetching = false;
static SemaphoreHandle_t s_album_art_mux = nullptr;

struct AlbumDecodeState {
  uint16_t *fb = nullptr;
  int src_w = 0;
  int src_h = 0;
  int crop_x = 0;
  int crop_y = 0;
  int crop_side = 0;
};

static AlbumDecodeState s_album_decode;

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

static bool album_art_lock(uint32_t ms = 50) {
  if (!s_album_art_mux) {
    s_album_art_mux = xSemaphoreCreateMutex();
  }
  return s_album_art_mux && xSemaphoreTake(s_album_art_mux, pdMS_TO_TICKS(ms)) == pdTRUE;
}

static void album_art_unlock() {
  if (s_album_art_mux) {
    xSemaphoreGive(s_album_art_mux);
  }
}

static void free_album_art_locked() {
  if (s_album_art_fb) {
    free(s_album_art_fb);
    s_album_art_fb = nullptr;
  }
  s_album_art_ready = false;
}

static void free_album_art() {
  if (!album_art_lock(250)) {
    return;
  }
  free_album_art_locked();
  album_art_unlock();
}

static bool fetch_bytes_https(const char *url, uint8_t **out_buf, size_t *out_len) {
  if (!url || !url[0] || !out_buf || !out_len) {
    return false;
  }
  *out_buf = nullptr;
  *out_len = 0;
  if (!pm_heap_tls_ready(kAlbumArtMinFetchHeap, "spotify art")) {
    Serial.println("spotify art: skipped low heap");
    return false;
  }

  std::unique_ptr<WiFiClientSecure> client(new (std::nothrow) WiFiClientSecure());
  std::unique_ptr<HTTPClient> http(new (std::nothrow) HTTPClient());
  if (!client || !http) {
    Serial.println("spotify art: alloc client failed");
    return false;
  }
  client->setInsecure();
  http->setTimeout(12000);
  http->setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!http->begin(*client, url)) {
    Serial.println("spotify art: HTTP begin failed");
    return false;
  }
  http->addHeader("Accept", "image/jpeg");
  const int code = http->GET();
  const int len = http->getSize();
  Serial.printf("spotify art: HTTP %d len=%d\n", code, len);
  if (code != 200 || len <= 0 || len > kAlbumArtMaxBytes) {
    http->end();
    return false;
  }
  uint8_t *buf = static_cast<uint8_t *>(pm_heap_alloc_response(static_cast<size_t>(len)));
  if (!buf) {
    http->end();
    Serial.println("spotify art: alloc image failed");
    return false;
  }
  WiFiClient *stream = http->getStreamPtr();
  size_t rd = 0;
  const uint32_t deadline = millis() + 7000u;
  while (rd < static_cast<size_t>(len)) {
    if (stream->available() > 0) {
      const int n = stream->readBytes(buf + rd, static_cast<size_t>(len) - rd);
      if (n > 0) {
        rd += static_cast<size_t>(n);
        continue;
      }
    }
    if (!http->connected() && stream->available() == 0) {
      break;
    }
    if (static_cast<int32_t>(millis() - deadline) >= 0) {
      break;
    }
    yield();
    delay(1);
  }
  http->end();
  if (rd < 8) {
    free(buf);
    Serial.printf("spotify art: short read %u/%d\n", static_cast<unsigned>(rd), len);
    return false;
  }
  *out_buf = buf;
  *out_len = rd;
  return true;
}

static int album_jpeg_draw(JPEGDRAW *pDraw) {
  if (!pDraw || !s_album_decode.fb || s_album_decode.crop_side <= 0) {
    return 0;
  }
  for (int y = 0; y < pDraw->iHeight; ++y) {
    const int sy = pDraw->y + y;
    if (sy < s_album_decode.crop_y || sy >= s_album_decode.crop_y + s_album_decode.crop_side) {
      continue;
    }
    const int dy = (sy - s_album_decode.crop_y) * kAlbumArtSize / s_album_decode.crop_side;
    for (int x = 0; x < pDraw->iWidth; ++x) {
      const int sx = pDraw->x + x;
      if (sx < s_album_decode.crop_x || sx >= s_album_decode.crop_x + s_album_decode.crop_side) {
        continue;
      }
      const int dx = (sx - s_album_decode.crop_x) * kAlbumArtSize / s_album_decode.crop_side;
      s_album_decode.fb[dy * kAlbumArtSize + dx] = pDraw->pPixels[y * pDraw->iWidth + x];
    }
  }
  return 1;
}

static bool decode_album_jpeg(const uint8_t *data, size_t len) {
  uint16_t *fb = static_cast<uint16_t *>(pm_heap_alloc_response(kAlbumArtSize * kAlbumArtSize * sizeof(uint16_t)));
  if (!fb) {
    Serial.println("spotify art: alloc fb failed");
    return false;
  }
  memset(fb, 0, kAlbumArtSize * kAlbumArtSize * sizeof(uint16_t));

  JPEGDEC jpg;
  s_album_decode = {};
  s_album_decode.fb = fb;
  if (jpg.openRAM(const_cast<uint8_t *>(data), static_cast<int>(len), album_jpeg_draw) != 1) {
    free(fb);
    s_album_decode = {};
    Serial.println("spotify art: jpeg open failed");
    return false;
  }
  s_album_decode.src_w = jpg.getWidth();
  s_album_decode.src_h = jpg.getHeight();
  s_album_decode.crop_side = min(s_album_decode.src_w, s_album_decode.src_h);
  s_album_decode.crop_x = (s_album_decode.src_w - s_album_decode.crop_side) / 2;
  s_album_decode.crop_y = (s_album_decode.src_h - s_album_decode.crop_side) / 2;
  jpg.setPixelType(RGB565_BIG_ENDIAN);
  const bool ok = jpg.decode(0, 0, 0) == 1;
  jpg.close();
  if (!ok) {
    free(fb);
    s_album_decode = {};
    Serial.println("spotify art: jpeg decode failed");
    return false;
  }
  if (!album_art_lock(250)) {
    free(fb);
    s_album_decode = {};
    Serial.println("spotify art: lock failed");
    return false;
  }
  free_album_art_locked();
  s_album_art_fb = fb;
  s_album_art_ready = true;
  album_art_unlock();
  const int src_w = s_album_decode.src_w;
  const int src_h = s_album_decode.src_h;
  s_album_decode = {};
  Serial.printf("spotify art: decoded %dx%d\n", src_w, src_h);
  return true;
}

static bool load_album_art_url(const char *url) {
  if (!url || !url[0]) {
    s_album_art_url[0] = '\0';
    free_album_art();
    return false;
  }
  if (strcmp(url, s_album_art_url) == 0) {
    return s_album_art_ready;
  }
  copy_trunc(s_album_art_url, sizeof(s_album_art_url), url);
  free_album_art();
  uint8_t *bytes = nullptr;
  size_t len = 0;
  const bool fetched = fetch_bytes_https(url, &bytes, &len);
  if (!fetched) {
    Serial.println("spotify art: fetch failed");
    return false;
  }
  const bool decoded = decode_album_jpeg(bytes, len);
  free(bytes);
  Serial.printf("spotify art: %s\n", decoded ? "ready" : "decode failed");
  return decoded;
}

struct AlbumArtTaskRequest {
  char url[192];
};

static void album_art_task(void *arg) {
  AlbumArtTaskRequest *req = static_cast<AlbumArtTaskRequest *>(arg);
  if (req && req->url[0] != '\0') {
    load_album_art_url(req->url);
  }
  free(req);
  s_album_art_fetching = false;
  vTaskDelete(nullptr);
}

static void schedule_album_art_load(const char *url) {
  if (!url || !url[0] || strcmp(url, s_album_art_url) == 0 || s_album_art_fetching) {
    return;
  }
  if (pm_heap_internal_largest() < 70000u) {
    copy_trunc(s_album_art_url, sizeof(s_album_art_url), url);
    Serial.printf("spotify art: deferred largest=%u\n", static_cast<unsigned>(pm_heap_internal_largest()));
    return;
  }
  AlbumArtTaskRequest *req = static_cast<AlbumArtTaskRequest *>(malloc(sizeof(AlbumArtTaskRequest)));
  if (!req) {
    return;
  }
  memset(req, 0, sizeof(*req));
  copy_trunc(req->url, sizeof(req->url), url);
  s_album_art_fetching = true;
  TaskHandle_t task = nullptr;
  constexpr uint32_t kAlbumArtTaskStack = 24576;
  const BaseType_t started =
      xTaskCreatePinnedToCore(album_art_task, "spotify_art", kAlbumArtTaskStack, req, 1, &task, 1);
  if (started != pdPASS) {
    s_album_art_fetching = false;
    free(req);
  }
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
  for (int i = 0; i < 18; ++i) {
    const int gr = r - 6 - i * 7;
    if (gr > kAlbumR + 8) {
      pm_gfx->drawCircle(cx, cy, gr, col);
    }
  }
}

static void draw_vinyl_texture(const PmSpotifyStreamItem *it, int cx, int cy, int r, float spin) {
  const uint16_t pit = pm_gfx->color565(54, 55, 62);
  const uint16_t glint = pm_gfx->color565(82, 82, 90);
  uint32_t seed = 2166136261u;
  for (const char *p = it ? it->title : nullptr; p && *p; ++p) {
    seed ^= static_cast<uint8_t>(*p);
    seed *= 16777619u;
  }
  for (int i = 0; i < 30; ++i) {
    seed = seed * 1664525u + 1013904223u;
    const float base = static_cast<float>(seed & 0xFFFFu) / 65535.f;
    seed = seed * 1664525u + 1013904223u;
    const int rr = kAlbumR + 12 + static_cast<int>((seed & 0xFFu) * (r - kAlbumR - 20) / 255u);
    const float a = base * pm_face_k_two_pi + spin;
    const int x = cx + static_cast<int>(lrintf(cosf(a) * static_cast<float>(rr)));
    const int y = cy + static_cast<int>(lrintf(sinf(a) * static_cast<float>(rr)));
    pm_gfx->drawPixel(x, y, (i % 5 == 0) ? glint : pit);
    if (i % 7 == 0) {
      const int x2 = cx + static_cast<int>(lrintf(cosf(a + 0.015f) * static_cast<float>(rr + 4)));
      const int y2 = cy + static_cast<int>(lrintf(sinf(a + 0.015f) * static_cast<float>(rr + 4)));
      pm_gfx->drawLine(x, y, x2, y2, glint);
    }
  }
}

static void clipped_label(char *out, size_t cap, const char *src, size_t max_chars) {
  if (!out || cap == 0) {
    return;
  }
  if (!src || !src[0]) {
    out[0] = '\0';
    return;
  }
  const size_t limit = (max_chars + 1 < cap) ? max_chars : cap - 1;
  strncpy(out, src, limit);
  out[limit] = '\0';
  if (strlen(src) > limit && limit > 1) {
    out[limit - 1] = '.';
  }
}

static void draw_album_cover_center(const PmSpotifyStreamItem *it, int cx, int cy, int r, float spin) {
  const uint16_t cover = it ? it->disc_rgb565 : pm_gfx->color565(80, 80, 90);
  const uint16_t hi = it ? it->highlight_rgb565 : pm_gfx->color565(210, 210, 220);
  pm_gfx->fillCircle(cx, cy, r + 8, pm_gfx->color565(7, 7, 9));
  pm_gfx->fillCircle(cx, cy, r + 4, pm_gfx->color565(218, 214, 190));
  pm_gfx->fillCircle(cx, cy, r, pm_gfx->color565(236, 229, 196));
  pm_gfx->drawCircle(cx, cy, r + 4, hi);

  const int art = r > 62 ? 54 : 44;
  const int art_x = cx - art / 2;
  const int art_y = cy - r + 17;
  pm_gfx->fillRoundRect(art_x - 2, art_y - 2, art + 4, art + 4, 4, pm_gfx->color565(18, 18, 22));
  const bool locked = album_art_lock(5);
  if (locked && s_album_art_ready && s_album_art_fb) {
    for (int y = 0; y < art; ++y) {
      const int sy = y * kAlbumArtSize / art;
      for (int x = 0; x < art; ++x) {
        const int sx = x * kAlbumArtSize / art;
        pm_gfx->drawPixel(art_x + x, art_y + y, s_album_art_fb[sy * kAlbumArtSize + sx]);
      }
    }
    album_art_unlock();
  } else {
    if (locked) {
      album_art_unlock();
    }
    pm_gfx->fillRoundRect(art_x, art_y, art, art, 4, cover);
    for (int i = 0; i < 4; ++i) {
      const int yy = art_y + 8 + i * (art / 5);
      pm_gfx->drawLine(art_x + 5, yy, art_x + art - 6, yy, (i == 1) ? pm_gfx->color565(245, 245, 235) : hi);
    }
  }

  char title[22];
  char artist[22];
  clipped_label(title, sizeof(title), it ? it->title : "", 20);
  clipped_label(artist, sizeof(artist), it ? it->artist : "", 20);
  if (!title[0]) {
    copy_trunc(title, sizeof(title), "Spotify");
  }
  pm_face_draw_centered_line(title, cy + 20, pm_gfx->color565(18, 18, 20), 1, 1);
  pm_face_draw_centered_line(artist[0] ? artist : "Now playing", cy + 36, pm_gfx->color565(72, 72, 68), 1, 1);

  pm_gfx->fillCircle(cx, cy, 15, pm_gfx->color565(12, 12, 14));
  pm_gfx->fillCircle(cx, cy, 9, pm_gfx->color565(230, 224, 196));
  pm_face_draw_centered_line("45", cy - 5, pm_gfx->color565(18, 18, 20), 1, 1);

  const int mark_r = r - 8;
  const int mx = cx + static_cast<int>(lrintf(cosf(spin + 0.8f) * static_cast<float>(mark_r)));
  const int my = cy + static_cast<int>(lrintf(sinf(spin + 0.8f) * static_cast<float>(mark_r)));
  pm_gfx->fillCircle(mx, my, 2, hi);
}

static void draw_spotify_backdrop() {
  pm_gfx->fillScreen(pm_gfx->color565(0, 0, 0));
  for (int r = kRecordR + 24; r > 28; r -= 20) {
    const uint8_t shade = static_cast<uint8_t>(8 + (kRecordR + 24 - r) / 7);
    pm_gfx->drawCircle(kCx, kCy, r, pm_gfx->color565(shade, shade, shade + 3));
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
  const uint16_t vinyl = pm_gfx->color565(10, 10, 13);
  const uint16_t groove = pm_gfx->color565(32, 32, 38);
  const float spin = (show_spin && is_playing_track && s_stream.is_playing)
                         ? static_cast<float>(now_ms % 12000u) / 12000.f * pm_face_k_two_pi
                         : static_cast<float>(s_stream.fake_progress_ms % 12000u) / 12000.f * pm_face_k_two_pi;

  pm_gfx->fillCircle(cx, cy, r + 10, pm_gfx->color565(2, 2, 3));
  pm_gfx->fillCircle(cx, cy, r, vinyl);
  pm_gfx->drawCircle(cx, cy, r, pm_gfx->color565(72, 72, 78));
  pm_gfx->drawCircle(cx, cy, r - 2, pm_gfx->color565(22, 22, 27));
  draw_grooves(cx, cy, r, groove);
  draw_vinyl_texture(it, cx, cy, r, spin);

  const float sheen_a = spin + 0.38f;
  const int sx0 = cx + static_cast<int>(cosf(sheen_a) * static_cast<float>(r - 16));
  const int sy0 = cy + static_cast<int>(sinf(sheen_a) * static_cast<float>(r - 16));
  const int sx1 = cx + static_cast<int>(cosf(sheen_a + 0.16f) * static_cast<float>(r - 54));
  const int sy1 = cy + static_cast<int>(sinf(sheen_a + 0.16f) * static_cast<float>(r - 54));
  pm_gfx->drawLine(sx0, sy0, sx1, sy1, pm_gfx->color565(92, 92, 102));
  pm_gfx->drawLine(sx0 + 1, sy0, sx1 + 1, sy1, pm_gfx->color565(48, 48, 56));

  draw_album_cover_center(it, cx, cy, kAlbumR, spin);

  if (show_spin && is_playing_track && s_stream.is_playing) {
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
    pm_face_draw_centered_line("PLAYING", LCD_HEIGHT - 24, it->highlight_rgb565, 1, 1);
  } else if (is_playing_track && !s_stream.is_playing) {
    const float prog =
        fmodf(static_cast<float>(s_stream.fake_progress_ms) / 218000.f, 1.f);
    draw_progress_arc(cx, cy, r, prog, pm_gfx->color565(90, 90, 96));
    pm_gfx->fillTriangle(cx - 10, cy - 4, cx - 10, cy + 12, cx + 14, cy + 4,
                         pm_gfx->color565(240, 238, 232));
    pm_face_draw_centered_line("PAUSED", LCD_HEIGHT - 24, pm_gfx->color565(150, 155, 162), 1, 1);
  } else {
    pm_face_draw_centered_line("TAP TO PLAY", LCD_HEIGHT - 24, pm_gfx->color565(200, 198, 190), 1, 1);
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
  if (hub->album_art_url[0] != '\0') {
    /* Secondary album-art HTTPS is disabled on S3 1.85 until artwork is proxied
       through the already-working Spotify status call. */
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
  draw_spotify_backdrop();

  if (pm_wifi_connected() && g_spotify_ui.error[0] != '\0' && !g_spotify_ui.ok) {
    char err[44];
    copy_trunc(err, sizeof(err), g_spotify_ui.error);
    pm_face_draw_centered_line(err, 28, pm_gfx->color565(255, 140, 120), 1, 1);
  } else if (!pm_wifi_connected()) {
    pm_face_draw_centered_line("Demo stream (no WiFi)", 28, pm_gfx->color565(110, 115, 122), 1, 1);
  }

  const int sel_i = s_stream.selected_index;
  draw_record(sel, sel_i == s_stream.playing_index, true,
              sel_i == s_stream.playing_index, millis());
  draw_now_marker();

  if (s_stream.hub_overlay && g_spotify_ui.device[0] != '\0') {
    char dev[40];
    snprintf(dev, sizeof(dev), "%.36s", g_spotify_ui.device);
    pm_face_draw_centered_line(dev, LCD_HEIGHT - 42, pm_gfx->color565(100, 108, 118), 1, 1);
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
    if (pm_wifi_connected() && s_stream.hub_overlay) {
      if (pm_spotify_command("previous", &g_spotify_ui)) {
        pm_face_spotify_sync_hub(&g_spotify_ui, true);
      }
      s_stream.last_browse_ms = now;
      snprintf(banner, banner_cap, "previous");
      return true;
    }
    s_stream.selected_index = clamp_index(s_stream.selected_index + 1);
    s_stream.last_browse_ms = now;
    recompute_mode();
    snprintf(banner, banner_cap, "browse +1");
    return true;
  }
  if (kind == PmGestureKind::SwipeDown) {
    if (pm_wifi_connected() && s_stream.hub_overlay) {
      if (pm_spotify_command("next", &g_spotify_ui)) {
        pm_face_spotify_sync_hub(&g_spotify_ui, true);
      }
      s_stream.last_browse_ms = now;
      snprintf(banner, banner_cap, "next");
      return true;
    }
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
