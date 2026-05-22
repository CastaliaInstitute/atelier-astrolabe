#include "pm_rocket.h"

#include <Arduino_GFX_Library.h>
#include <JPEGDEC.h>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <time.h>

#include "esp_log.h"
#include "pin_config.h"
#include "pm_config.h"
#include "pm_display.h"
#include "pm_heap.h"
#include "pm_http.h"
#include "pm_resource.h"

static const char *TAG = "pm_rocket";

static const char *kLl2UpcomingUrl =
    "https://fdo.rocketlaunch.live/json/launches/next/5";

static const char *kLl2UserAgent = "Astrolabe/1.0 (Castalia Institute)";

static constexpr int64_t kHorizonSec = 14 * 24 * 3600;

static uint16_t *s_pad_fb = nullptr;
static int s_pad_w = 0;
static int s_pad_h = 0;
static bool s_pad_ready = false;
static char s_pad_launch_id[40] = "";
static SemaphoreHandle_t s_pad_mux = nullptr;

static TaskHandle_t s_fetch_task = nullptr;
static SemaphoreHandle_t s_fetch_mux = nullptr;
static volatile bool s_fetch_busy = false;
static volatile bool s_fetch_done = false;
static PmRocketStatus s_fetch_result = {};

static constexpr uint32_t kRocketFetchTaskStack = 32768;
static constexpr time_t kStarship12LaunchEpoch = 1779406200;  // 2026-05-21 23:30:00 UTC
static constexpr time_t kStarship12WindowCloseEpoch = 1779411600;  // 2026-05-22 01:00:00 UTC

static bool fetch_starship12_media_timeline(PmRocketStatus *out);

static bool rocket_heap_ready(uint32_t min_free, uint32_t min_largest, const char *tag) {
  const uint32_t free_i = pm_heap_internal_free();
  const uint32_t largest_i = pm_heap_internal_largest();
  if (free_i < min_free || largest_i < min_largest) {
    ESP_LOGW(TAG, "%s low heap: internal=%u largest=%u psram=%u need=%u/%u",
             tag ? tag : "rocket", static_cast<unsigned>(free_i), static_cast<unsigned>(largest_i),
             static_cast<unsigned>(pm_heap_psram_free()), static_cast<unsigned>(min_free),
             static_cast<unsigned>(min_largest));
    return false;
  }
  return true;
}

static bool is_starship_flight_12(const PmRocketLaunch &launch) {
  return strstr(launch.name, "Starship Flight 12") != nullptr ||
         strstr(launch.name, "Starship | Flight 12") != nullptr;
}

static void copy_starship12_stream_url(PmRocketLaunch *launch) {
  if (!launch) {
    return;
  }
  if (strlen(MYNAH_SUPABASE_URL) > 0) {
    char base[96];
    strncpy(base, MYNAH_SUPABASE_URL, sizeof(base) - 1);
    base[sizeof(base) - 1] = '\0';
    size_t n = strlen(base);
    while (n > 0 && base[n - 1] == '/') {
      base[--n] = '\0';
    }
    snprintf(launch->webcast_url, sizeof(launch->webcast_url),
             "%s/functions/v1/media-stream?l=s12&r=1", base);
    return;
  }
  strncpy(launch->webcast_url, "https://www.spacex.com/launches/starship-flight-12",
          sizeof(launch->webcast_url) - 1);
  launch->webcast_url[sizeof(launch->webcast_url) - 1] = '\0';
}

static void maybe_apply_starship12_webcast(PmRocketLaunch *launch) {
  if (!launch || !launch->valid || !is_starship_flight_12(*launch)) {
    return;
  }
  launch->net_unix = static_cast<int64_t>(kStarship12LaunchEpoch);
  if (launch->webcast_url[0] != '\0') {
    return;
  }
  copy_starship12_stream_url(launch);
}

static void rocket_timeline_add(PmRocketStatus *out, int32_t offset_sec, const char *label) {
  if (!out || !label || !label[0] || out->timeline_count >= kPmRocketMaxTimelineEvents) {
    return;
  }
  PmRocketTimelineEvent &e = out->timeline[out->timeline_count++];
  e.valid = true;
  e.offset_sec = offset_sec;
  strncpy(e.label, label, sizeof(e.label) - 1);
  e.label[sizeof(e.label) - 1] = '\0';
}

static void fill_starship12_static_timeline(PmRocketStatus *out) {
  if (!out || out->timeline_count > 0) {
    return;
  }
  rocket_timeline_add(out, -3000, "GO poll");
  rocket_timeline_add(out, -2333, "Ship LOX");
  rocket_timeline_add(out, -2100, "Booster LOX");
  rocket_timeline_add(out, -1979, "Ship fuel");
  rocket_timeline_add(out, -1290, "Raptor chill");
  rocket_timeline_add(out, -170, "Booster load done");
  rocket_timeline_add(out, -30, "GO for launch");
  rocket_timeline_add(out, 0, "Liftoff");
  rocket_timeline_add(out, 45, "Max Q");
  rocket_timeline_add(out, 142, "MECO");
  rocket_timeline_add(out, 144, "Hot stage");
  rocket_timeline_add(out, 491, "Ship cutoff");
  rocket_timeline_add(out, 1057, "Payload deploy");
  rocket_timeline_add(out, 2317, "Relight demo");
  rocket_timeline_add(out, 2867, "Entry");
  rocket_timeline_add(out, 3906, "Landing burn");
  rocket_timeline_add(out, 3908, "Landing flip");
  rocket_timeline_add(out, 3926, "Landing");
}

static bool fill_starship12_demo(PmRocketStatus *out, const char *reason) {
  if (!out) {
    return false;
  }
  const time_t now_epoch = time(nullptr);
  if (now_epoch > kStarship12WindowCloseEpoch + 2 * 3600) {
    return false;
  }

  memset(out, 0, sizeof(*out));
  PmRocketLaunch &launch = out->launches[0];
  launch.valid = true;
  strncpy(launch.id, "starship-flight-12-demo", sizeof(launch.id) - 1);
  strncpy(launch.name, "Starship Flight 12", sizeof(launch.name) - 1);
  strncpy(launch.vehicle, "Starship", sizeof(launch.vehicle) - 1);
  strncpy(launch.provider, "SpaceX", sizeof(launch.provider) - 1);
  strncpy(launch.pad, "Orbital Pad B", sizeof(launch.pad) - 1);
  strncpy(launch.location, "Starbase, Texas", sizeof(launch.location) - 1);
  strncpy(launch.status_abbrev, reason && reason[0] ? "Fallback" : "Scheduled", sizeof(launch.status_abbrev) - 1);
  launch.net_unix = static_cast<int64_t>(kStarship12LaunchEpoch);
  maybe_apply_starship12_webcast(&launch);
  if (!fetch_starship12_media_timeline(out)) {
    fill_starship12_static_timeline(out);
  }
  out->ok = true;
  out->count = 1;
  if (reason && reason[0]) {
    snprintf(out->error, sizeof(out->error), "fallback: %.70s", reason);
  }
  return true;
}

static bool extract_json_string_field(const char *json, const char *key, char *out, size_t out_cap) {
  char pat[48];
  snprintf(pat, sizeof(pat), "\"%s\":\"", key);
  const char *p = strstr(json, pat);
  if (!p) {
    return false;
  }
  p += strlen(pat);
  size_t o = 0;
  while (*p && *p != '"' && o + 1 < out_cap) {
    if (*p == '\\' && p[1]) {
      ++p;
    }
    out[o++] = *p++;
  }
  out[o] = '\0';
  return o > 0;
}

static time_t utc_iso8601_to_epoch(const char *iso) {
  if (!iso || !iso[0]) {
    return 0;
  }
  int y = 0;
  int mo = 0;
  int d = 0;
  int h = 0;
  int mi = 0;
  int s = 0;
  if (sscanf(iso, "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &s) < 6) {
    return 0;
  }
  struct tm u = {};
  u.tm_year = y - 1900;
  u.tm_mon = mo - 1;
  u.tm_mday = d;
  u.tm_hour = h;
  u.tm_min = mi;
  u.tm_sec = s;
  u.tm_isdst = 0;
  const char *prev = getenv("TZ");
  char saved[48] = {};
  if (prev) {
    strncpy(saved, prev, sizeof(saved) - 1);
  }
  setenv("TZ", "UTC0", 1);
  tzset();
  const time_t e = mktime(&u);
  if (prev) {
    setenv("TZ", saved, 1);
  } else {
    unsetenv("TZ");
  }
  tzset();
  return e;
}

static bool launch_block_is_past_success(const char *block, time_t net_epoch, time_t now_epoch) {
  char abbrev[16];
  abbrev[0] = '\0';
  (void)extract_json_string_field(block, "abbrev", abbrev, sizeof(abbrev));
  if (strcmp(abbrev, "Success") == 0 && net_epoch > 0 && net_epoch <= now_epoch) {
    return true;
  }
  if (net_epoch > 0 && net_epoch < now_epoch - 1800) {
    return true;
  }
  return false;
}

static bool parse_launch_block(const char *block, size_t block_len, time_t now_epoch, PmRocketLaunch *out) {
  if (!block || block_len < 32 || !out) {
    return false;
  }
  char *slice = static_cast<char *>(malloc(block_len + 1));
  if (!slice) {
    return false;
  }
  memcpy(slice, block, block_len);
  slice[block_len] = '\0';

  char net_iso[32];
  net_iso[0] = '\0';
  if (!extract_json_string_field(slice, "net", net_iso, sizeof(net_iso))) {
    free(slice);
    return false;
  }
  const time_t net_epoch = utc_iso8601_to_epoch(net_iso);
  if (net_epoch <= 0) {
    free(slice);
    return false;
  }
  if (launch_block_is_past_success(slice, net_epoch, now_epoch)) {
    free(slice);
    return false;
  }

  memset(out, 0, sizeof(*out));
  out->net_unix = static_cast<int64_t>(net_epoch);
  (void)extract_json_string_field(slice, "id", out->id, sizeof(out->id));
  (void)extract_json_string_field(slice, "name", out->name, sizeof(out->name));
  (void)extract_json_string_field(slice, "abbrev", out->status_abbrev, sizeof(out->status_abbrev));
  out->webcast_live = strstr(slice, "\"webcast_live\":true") != nullptr;

  const char *rocket = strstr(slice, "\"rocket\":");
  if (rocket) {
    const char *full = strstr(rocket, "\"full_name\":\"");
    if (full) {
      full += strlen("\"full_name\":\"");
      size_t o = 0;
      while (*full && *full != '"' && o + 1 < sizeof(out->vehicle)) {
        out->vehicle[o++] = *full++;
      }
      out->vehicle[o] = '\0';
    }
    if (!out->vehicle[0]) {
      const char *cfg_name = strstr(rocket, "\"configuration\":");
      if (cfg_name) {
        const char *nm = strstr(cfg_name, "\"name\":\"");
        if (nm) {
          nm += strlen("\"name\":\"");
          size_t o = 0;
          while (*nm && *nm != '"' && o + 1 < sizeof(out->vehicle)) {
            out->vehicle[o++] = *nm++;
          }
          out->vehicle[o] = '\0';
        }
      }
    }
  }

  const char *lsp = strstr(slice, "\"launch_service_provider\":");
  if (lsp) {
    const char *nm = strstr(lsp, "\"name\":\"");
    if (nm) {
      nm += strlen("\"name\":\"");
      size_t o = 0;
      while (*nm && *nm != '"' && o + 1 < sizeof(out->provider)) {
        out->provider[o++] = *nm++;
      }
      out->provider[o] = '\0';
    }
  }

  const char *pad = strstr(slice, "\"pad\":");
  if (pad) {
    const char *nm = strstr(pad, "\"name\":\"");
    if (nm) {
      nm += strlen("\"name\":\"");
      size_t o = 0;
      while (*nm && *nm != '"' && o + 1 < sizeof(out->pad)) {
        out->pad[o++] = *nm++;
      }
      out->pad[o] = '\0';
    }
    const char *loc = strstr(pad, "\"location\":");
    if (loc) {
      const char *ln = strstr(loc, "\"name\":\"");
      if (ln) {
        ln += strlen("\"name\":\"");
        size_t o = 0;
        while (*ln && *ln != '"' && o + 1 < sizeof(out->location)) {
          out->location[o++] = *ln++;
        }
        out->location[o] = '\0';
      }
    }
  }

  free(slice);
  if (!out->name[0]) {
    return false;
  }
  out->valid = true;
  return true;
}

static void pad_mux_ensure(void) {
  if (!s_pad_mux) {
    s_pad_mux = xSemaphoreCreateMutex();
  }
}

static bool pad_mux_take(uint32_t timeout_ms = 1000) {
  pad_mux_ensure();
  return !s_pad_mux || xSemaphoreTake(s_pad_mux, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

static void pad_mux_give(void) {
  if (s_pad_mux) {
    xSemaphoreGive(s_pad_mux);
  }
}

static void pad_image_free_fb_locked(void) {
  if (s_pad_fb) {
    free(s_pad_fb);
    s_pad_fb = nullptr;
  }
  s_pad_w = 0;
  s_pad_h = 0;
  s_pad_ready = false;
}

static void pad_image_free_fb(void) {
  if (!pad_mux_take()) {
    return;
  }
  pad_image_free_fb_locked();
  pad_mux_give();
}

void pm_rocket_pad_image_release(void) {
  pad_image_free_fb();
  if (!pad_mux_take()) {
    return;
  }
  s_pad_launch_id[0] = '\0';
  pad_mux_give();
}

bool pm_rocket_pad_image_ready(void) {
  if (!pad_mux_take(50)) {
    return false;
  }
  const bool ready = s_pad_ready && s_pad_fb && s_pad_w > 0 && s_pad_h > 0;
  pad_mux_give();
  return ready;
}

static bool copy_json_quoted_url(const char *p, char *out, size_t cap) {
  if (!p || !out || cap == 0) {
    return false;
  }
  size_t o = 0;
  while (*p && *p != '"' && o + 1 < cap) {
    if (*p == '\\' && p[1]) {
      ++p;
    }
    out[o++] = *p++;
  }
  out[o] = '\0';
  return o > 0 && strncmp(out, "http", 4) == 0;
}

static bool parse_launch_image_url(const char *json, char *out, size_t cap) {
  if (!json || !out || cap == 0) {
    return false;
  }
  out[0] = '\0';
  const char *p = json;
  while ((p = strstr(p, "\"image\":")) != nullptr) {
    p += strlen("\"image\":");
    while (*p == ' ' || *p == '\t') {
      ++p;
    }
    if (*p == '"') {
      if (copy_json_quoted_url(p + 1, out, cap)) {
        return true;
      }
    }
    ++p;
  }
  const char *pad = strstr(json, "\"pad\":");
  if (pad) {
    const char *mi = strstr(pad, "\"map_image\":\"");
    if (mi) {
      return copy_json_quoted_url(mi + strlen("\"map_image\":\""), out, cap);
    }
  }
  return false;
}

typedef struct {
  uint8_t *buf;
  size_t cap;
  size_t len;
} RocketBinaryDownload;

static bool rocket_binary_on_data(const uint8_t *data, size_t len, void *ctx) {
  RocketBinaryDownload *dl = static_cast<RocketBinaryDownload *>(ctx);
  if (!dl || !data || len == 0) {
    return false;
  }
  if (dl->len + len > dl->cap) {
    return false;
  }
  memcpy(dl->buf + dl->len, data, len);
  dl->len += len;
  return true;
}

static bool rocket_http_get_text_alloc(const char *url, const char *accept, char **out_resp, size_t *out_rd) {
  if (!url || !out_resp || !out_rd) {
    return false;
  }
  *out_resp = nullptr;
  *out_rd = 0;
  char *resp = static_cast<char *>(pm_heap_alloc_response(MYNAH_ROCKET_MAX_BYTES + 1u));
  if (!resp) {
    return false;
  }
  const PmHttpHeader headers[] = {
      {"User-Agent", kLl2UserAgent},
      {"Accept", accept ? accept : "application/json"},
  };
  PmHttpTextResult result = {};
  const bool ok = pm_http_request_text(url, "GET", nullptr, headers, sizeof(headers) / sizeof(headers[0]), resp,
                                       MYNAH_ROCKET_MAX_BYTES + 1u, MYNAH_ROCKET_HTTP_MS, &result);
  if (!ok) {
    ESP_LOGW(TAG, "GET %s HTTP %d len %u", url, result.status_code, static_cast<unsigned>(result.bytes_read));
    free(resp);
    return false;
  }
  *out_rd = result.bytes_read;
  *out_resp = resp;
  return true;
}

static bool rocket_http_download_binary(const char *url, uint8_t **out_buf, size_t *out_len) {
  if (!url || !url[0] || !out_buf || !out_len) {
    return false;
  }
  *out_buf = nullptr;
  *out_len = 0;
  if (!rocket_heap_ready(MYNAH_ROCKET_DETAIL_MIN_FETCH_HEAP, MYNAH_ROCKET_MIN_LARGEST_INTERNAL, "rocket image")) {
    return false;
  }
  RocketBinaryDownload dl = {};
  dl.cap = MYNAH_ROCKET_IMAGE_MAX_BYTES;
  dl.buf = static_cast<uint8_t *>(pm_heap_alloc_response(dl.cap));
  if (!dl.buf) {
    return false;
  }
  const PmHttpHeader headers[] = {
      {"User-Agent", kLl2UserAgent},
      {"Accept", "image/jpeg,image/png,image/*;q=0.8,*/*;q=0.1"},
  };
  PmHttpTextResult result = {};
  const bool ok = pm_http_request_stream(url, "GET", nullptr, headers, sizeof(headers) / sizeof(headers[0]),
                                         MYNAH_ROCKET_HTTP_MS, rocket_binary_on_data, &dl, &result);
  if (!ok || dl.len < 8) {
    ESP_LOGW(TAG, "image stream HTTP %d len %u", result.status_code, static_cast<unsigned>(result.bytes_read));
    free(dl.buf);
    return false;
  }
  *out_buf = dl.buf;
  *out_len = dl.len;
  return true;
}

static int pad_jpeg_draw(JPEGDRAW *pDraw) {
  if (!s_pad_fb || s_pad_w <= 0 || s_pad_h <= 0 || !pDraw) {
    return 0;
  }
  for (int row = 0; row < pDraw->iHeight; ++row) {
    uint16_t *dst = s_pad_fb + (pDraw->y + row) * s_pad_w + pDraw->x;
    const uint16_t *src = pDraw->pPixels + row * pDraw->iWidth;
    memcpy(dst, src, static_cast<size_t>(pDraw->iWidth) * sizeof(uint16_t));
  }
  return 1;
}

static int jpeg_pick_scale(int w, int h) {
  const int max_edge = max(w, h);
  if (max_edge <= MYNAH_ROCKET_IMAGE_MAX_DIM) {
    return 0;
  }
  if (max_edge / 2 <= MYNAH_ROCKET_IMAGE_MAX_DIM) {
    return JPEG_SCALE_HALF;
  }
  if (max_edge / 4 <= MYNAH_ROCKET_IMAGE_MAX_DIM) {
    return JPEG_SCALE_QUARTER;
  }
  return JPEG_SCALE_EIGHTH;
}

static bool pad_decode_jpeg(const uint8_t *data, size_t len) {
  if (!pad_mux_take(3000)) {
    return false;
  }
  pad_image_free_fb_locked();
  JPEGDEC jpg;
  if (jpg.openRAM(const_cast<uint8_t *>(data), static_cast<int>(len), pad_jpeg_draw) != 1) {
    pad_mux_give();
    return false;
  }
  const int scale = jpeg_pick_scale(jpg.getWidth(), jpg.getHeight());
  int w = jpg.getWidth();
  int h = jpg.getHeight();
  if (scale == JPEG_SCALE_HALF) {
    w /= 2;
    h /= 2;
  } else if (scale == JPEG_SCALE_QUARTER) {
    w /= 4;
    h /= 4;
  } else if (scale == JPEG_SCALE_EIGHTH) {
    w /= 8;
    h /= 8;
  }
  if (w <= 0 || h <= 0 || w > 512 || h > 512) {
    jpg.close();
    pad_mux_give();
    return false;
  }
  s_pad_w = w;
  s_pad_h = h;
  const size_t px = static_cast<size_t>(w) * static_cast<size_t>(h);
  s_pad_fb = static_cast<uint16_t *>(pm_heap_alloc_response(px * sizeof(uint16_t)));
  if (!s_pad_fb) {
    jpg.close();
    pad_image_free_fb_locked();
    pad_mux_give();
    return false;
  }
  memset(s_pad_fb, 0, px * sizeof(uint16_t));
  jpg.setPixelType(RGB565_BIG_ENDIAN);
  if (jpg.decode(0, 0, scale) != 1) {
    jpg.close();
    pad_image_free_fb_locked();
    pad_mux_give();
    return false;
  }
  jpg.close();
  s_pad_ready = true;
  pad_mux_give();
  return true;
}

static bool fetch_pad_image_for_url(const char *url) {
  uint8_t *img = nullptr;
  size_t img_len = 0;
  if (!rocket_http_download_binary(url, &img, &img_len) || !img) {
    return false;
  }
  const bool ok = pad_decode_jpeg(img, img_len);
  free(img);
  if (ok) {
    ESP_LOGI(TAG, "pad image %dx%d", s_pad_w, s_pad_h);
  } else {
    ESP_LOGW(TAG, "pad JPEG decode failed");
  }
  return ok;
}

static uint16_t blend565_fast(uint16_t bg, uint16_t fg, float alpha) {
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
  return static_cast<uint16_t>(
      (((static_cast<uint32_t>(br * ia + fr * alpha) / 255) & 0x1F) << 11) |
      (((static_cast<uint32_t>(bg_g * ia + fg_g * alpha) / 255) & 0x3F) << 5) |
      ((static_cast<uint32_t>(bb * ia + fb * alpha) / 255) & 0x1F));
}

void pm_rocket_pad_image_draw_background(int cx, int cy, int cover_radius, uint16_t bg_color, float dim_alpha) {
  if (cover_radius <= 0 || !pad_mux_take(50)) {
    return;
  }
  if (!(s_pad_ready && s_pad_fb && s_pad_w > 0 && s_pad_h > 0)) {
    pad_mux_give();
    return;
  }
  const float img_alpha = 1.f - dim_alpha;
  if (img_alpha <= 0.f) {
    pad_mux_give();
    return;
  }

  const int diam = cover_radius * 2;
  const int r2 = cover_radius * cover_radius;
  const int crop = (s_pad_w >= s_pad_h) ? ((s_pad_w - s_pad_h) / 2) : 0;
  const int crop_y = (s_pad_h > s_pad_w) ? ((s_pad_h - s_pad_w) / 2) : 0;
  const int square = min(s_pad_w, s_pad_h);

  for (int dy = -cover_radius; dy < cover_radius; ++dy) {
    const int y = cy + dy;
    if (y < 0 || y >= LCD_HEIGHT) {
      continue;
    }
    const int dy2 = dy * dy;
    for (int dx = -cover_radius; dx < cover_radius; ++dx) {
      if (dx * dx + dy2 > r2) {
        continue;
      }
      const int x = cx + dx;
      if (x < 0 || x >= LCD_WIDTH) {
        continue;
      }
      const int sx = crop + (dx + cover_radius) * square / diam;
      const int sy = crop_y + (dy + cover_radius) * square / diam;
      if (sx < 0 || sx >= s_pad_w || sy < 0 || sy >= s_pad_h) {
        continue;
      }
      const uint16_t fg = s_pad_fb[sy * s_pad_w + sx];
      const uint16_t pix = blend565_fast(bg_color, fg, img_alpha);
      pm_gfx->writePixel(x, y, pix);
    }
  }
  pad_mux_give();
}

static bool parse_webcast_from_detail_json(const char *json, PmRocketLaunch *out) {
  if (!json || !out) {
    return false;
  }
  out->webcast_url[0] = '\0';
  out->webcast_live = strstr(json, "\"webcast_live\":true") != nullptr;

  const char *vid = strstr(json, "\"vidURLs\":");
  if (!vid) {
    return false;
  }
  vid += strlen("\"vidURLs\":");
  while (*vid == ' ') {
    ++vid;
  }
  if (*vid == 'n' || (*vid == '[' && vid[1] == ']')) {
    return false;
  }

  const char *end = strstr(vid, "\"infoURLs\":");
  if (!end) {
    end = json + strlen(json);
  }

  const char *url_key = vid;
  while ((url_key = strstr(url_key, "\"url\":\"")) != nullptr && url_key < end) {
    const char *p = url_key + strlen("\"url\":\"");
    if (strncmp(p, "http", 4) != 0) {
      ++url_key;
      continue;
    }
    size_t src = 0;
    size_t dst = 0;
    while (p[src] && p[src] != '"' && dst + 1 < sizeof(out->webcast_url)) {
      if (p[src] == '\\' && p[src + 1]) {
        ++src;
        continue;
      }
      out->webcast_url[dst++] = p[src++];
    }
    out->webcast_url[dst] = '\0';
    return dst > 0;
  }
  return false;
}

static bool extract_json_int_field_from(const char *json, const char *key, int32_t *out) {
  if (!json || !key || !out) {
    return false;
  }
  char pat[48];
  snprintf(pat, sizeof(pat), "\"%s\":", key);
  const char *p = strstr(json, pat);
  if (!p) {
    return false;
  }
  p += strlen(pat);
  while (*p == ' ' || *p == '\t' || *p == '"') {
    ++p;
  }
  char *end = nullptr;
  const long v = strtol(p, &end, 10);
  if (!end || end == p) {
    return false;
  }
  *out = static_cast<int32_t>(v);
  return true;
}

static void parse_media_timeline_json(const char *json, PmRocketStatus *out) {
  if (!json || !out) {
    return;
  }
  const char *timeline = strstr(json, "\"timeline\":");
  if (!timeline) {
    return;
  }
  const char *entries = strstr(timeline, "\"entries\":");
  if (!entries) {
    return;
  }
  out->timeline_count = 0;
  const char *p = entries;
  while (out->timeline_count < kPmRocketMaxTimelineEvents && (p = strstr(p, "\"offsetSeconds\":")) != nullptr) {
    const char *end = strstr(p + 1, "\"offsetSeconds\":");
    if (!end) {
      end = json + strlen(json);
    }
    int32_t offset = 0;
    char label[56];
    label[0] = '\0';
    if (extract_json_int_field_from(p, "offsetSeconds", &offset) &&
        extract_json_string_field(p, "label", label, sizeof(label))) {
      rocket_timeline_add(out, offset, label);
    }
    p = end;
  }
}

static bool fetch_starship12_media_timeline(PmRocketStatus *out) {
  if (!out || strlen(MYNAH_SUPABASE_URL) == 0) {
    return false;
  }
  char base[96];
  strncpy(base, MYNAH_SUPABASE_URL, sizeof(base) - 1);
  base[sizeof(base) - 1] = '\0';
  size_t n = strlen(base);
  while (n > 0 && base[n - 1] == '/') {
    base[--n] = '\0';
  }
  char url[160];
  snprintf(url, sizeof(url), "%s/functions/v1/media-stream?l=s12", base);
  if (!rocket_heap_ready(MYNAH_ROCKET_DETAIL_MIN_FETCH_HEAP, MYNAH_ROCKET_MIN_LARGEST_INTERNAL, "rocket timeline")) {
    return false;
  }

  char *resp = nullptr;
  size_t rd = 0;
  if (!rocket_http_get_text_alloc(url, "application/json", &resp, &rd)) {
    return false;
  }
  parse_media_timeline_json(resp, out);
  char image_url[256];
  image_url[0] = '\0';
  const bool has_image_url = extract_json_string_field(resp, "imageUrl", image_url, sizeof(image_url)) && image_url[0];
  free(resp);
  resp = nullptr;
  if (has_image_url) {
    const bool launch_changed = strcmp(s_pad_launch_id, "starship-flight-12") != 0;
    if (launch_changed || !pm_rocket_pad_image_ready()) {
      if (launch_changed) {
        pm_rocket_pad_image_release();
      }
      if (fetch_pad_image_for_url(image_url)) {
        strncpy(s_pad_launch_id, "starship-flight-12", sizeof(s_pad_launch_id) - 1);
        s_pad_launch_id[sizeof(s_pad_launch_id) - 1] = '\0';
      }
    }
  }
  ESP_LOGI(TAG, "timeline events=%d", out->timeline_count);
  return out->timeline_count > 0;
}

static bool fetch_launch_detail(PmRocketLaunch *launch) {
  if (!launch || !launch->valid || launch->id[0] == '\0') {
    return false;
  }

  char url[120];
  snprintf(url, sizeof(url), "https://ll.thespacedevs.com/2.2.0/launch/%s/", launch->id);
  if (!rocket_heap_ready(MYNAH_ROCKET_DETAIL_MIN_FETCH_HEAP, MYNAH_ROCKET_MIN_LARGEST_INTERNAL, "rocket detail")) {
    return false;
  }

  char *resp = nullptr;
  size_t rd = 0;
  if (!rocket_http_get_text_alloc(url, "application/json", &resp, &rd)) {
    return false;
  }

  const bool webcast_ok = parse_webcast_from_detail_json(resp, launch);

  char img_url[256];
  img_url[0] = '\0';
  if (parse_launch_image_url(resp, img_url, sizeof(img_url))) {
    const bool launch_changed = strcmp(s_pad_launch_id, launch->id) != 0;
    if (launch_changed || !pm_rocket_pad_image_ready()) {
      if (launch_changed) {
        pm_rocket_pad_image_release();
      }
      if (fetch_pad_image_for_url(img_url)) {
        strncpy(s_pad_launch_id, launch->id, sizeof(s_pad_launch_id) - 1);
        s_pad_launch_id[sizeof(s_pad_launch_id) - 1] = '\0';
      }
    }
  }

  free(resp);
  return webcast_ok;
}

static void insert_launch_sorted(PmRocketLaunch *list, int *count, const PmRocketLaunch *launch) {
  if (!launch || !launch->valid) {
    return;
  }
  int slot = *count;
  for (int i = 0; i < *count; ++i) {
    if (launch->net_unix < list[i].net_unix) {
      slot = i;
      break;
    }
  }
  if (*count < kPmRocketMaxLaunches) {
    for (int i = *count; i > slot; --i) {
      list[i] = list[i - 1];
    }
    (*count)++;
    list[slot] = *launch;
    return;
  }
  if (slot >= kPmRocketMaxLaunches) {
    return;
  }
  for (int i = kPmRocketMaxLaunches - 1; i > slot; --i) {
    list[i] = list[i - 1];
  }
  list[slot] = *launch;
}

static int collect_upcoming_launches(const char *json, PmRocketLaunch *list, int list_cap) {
  const char *results = strstr(json, "\"results\":");
  if (!results || list_cap <= 0) {
    return 0;
  }
  const time_t now_epoch = time(nullptr);
  const int64_t horizon = static_cast<int64_t>(now_epoch) + kHorizonSec;
  int count = 0;

  const char *p = results;
  while ((p = strstr(p, "{\"id\":")) != nullptr) {
    const char *next = strstr(p + 8, "{\"id\":");
    const char *end = next ? next : json + strlen(json);
    const size_t n = static_cast<size_t>(end - p);
    PmRocketLaunch scratch = {};
    if (parse_launch_block(p, n, now_epoch, &scratch) && scratch.net_unix <= horizon) {
      insert_launch_sorted(list, &count, &scratch);
    }
    if (!next) {
      break;
    }
    p = next;
  }
  return count;
}

static bool extract_json_number_field(const char *json, const char *key, long long *out) {
  if (!json || !key || !out) {
    return false;
  }
  char pat[48];
  snprintf(pat, sizeof(pat), "\"%s\":", key);
  const char *p = strstr(json, pat);
  if (!p) {
    return false;
  }
  p += strlen(pat);
  while (*p == ' ' || *p == '\t' || *p == '"') {
    ++p;
  }
  char *end = nullptr;
  const long long v = strtoll(p, &end, 10);
  if (!end || end == p) {
    return false;
  }
  *out = v;
  return true;
}

static void extract_nested_name(const char *slice, const char *obj_key, char *out, size_t cap) {
  if (!slice || !obj_key || !out || cap == 0) {
    return;
  }
  char pat[48];
  snprintf(pat, sizeof(pat), "\"%s\":", obj_key);
  const char *obj = strstr(slice, pat);
  if (obj) {
    (void)extract_json_string_field(obj, "name", out, cap);
  }
}

static bool parse_rll_launch_block(const char *block, size_t block_len, time_t now_epoch, PmRocketLaunch *out) {
  if (!block || block_len < 32 || !out) {
    return false;
  }
  char *slice = static_cast<char *>(malloc(block_len + 1));
  if (!slice) {
    return false;
  }
  memcpy(slice, block, block_len);
  slice[block_len] = '\0';

  long long id = 0;
  long long sort_date = 0;
  if (!extract_json_number_field(slice, "id", &id) || !extract_json_number_field(slice, "sort_date", &sort_date)) {
    free(slice);
    return false;
  }
  if (sort_date <= 0 || sort_date < static_cast<long long>(now_epoch) - 1800) {
    free(slice);
    return false;
  }

  memset(out, 0, sizeof(*out));
  snprintf(out->id, sizeof(out->id), "rll-%lld", id);
  out->net_unix = static_cast<int64_t>(sort_date);
  (void)extract_json_string_field(slice, "name", out->name, sizeof(out->name));
  extract_nested_name(slice, "provider", out->provider, sizeof(out->provider));
  extract_nested_name(slice, "vehicle", out->vehicle, sizeof(out->vehicle));
  extract_nested_name(slice, "pad", out->pad, sizeof(out->pad));
  const char *pad = strstr(slice, "\"pad\":");
  if (pad) {
    extract_nested_name(pad, "location", out->location, sizeof(out->location));
  }
  strncpy(out->status_abbrev, "Scheduled", sizeof(out->status_abbrev) - 1);
  free(slice);
  if (!out->name[0]) {
    return false;
  }
  out->valid = true;
  maybe_apply_starship12_webcast(out);
  return true;
}

static int collect_rll_launches(const char *json, PmRocketLaunch *list, int list_cap) {
  const char *result = strstr(json, "\"result\":");
  if (!result || list_cap <= 0) {
    return 0;
  }
  const time_t now_epoch = time(nullptr);
  int count = 0;
  const char *p = result;
  while ((p = strstr(p, "{\"id\":")) != nullptr) {
    const char *next = strstr(p + 8, "{\"id\":");
    const char *end = next ? next : json + strlen(json);
    PmRocketLaunch scratch = {};
    if (parse_rll_launch_block(p, static_cast<size_t>(end - p), now_epoch, &scratch)) {
      insert_launch_sorted(list, &count, &scratch);
    }
    if (!next) {
      break;
    }
    p = next;
  }
  return count;
}

const PmRocketLaunch *pm_rocket_next(const PmRocketStatus *status) {
  if (!status || !status->ok || status->count <= 0) {
    return nullptr;
  }
  for (int i = 0; i < status->count; ++i) {
    if (status->launches[i].valid) {
      return &status->launches[i];
    }
  }
  return nullptr;
}

bool pm_rocket_fetch(PmRocketStatus *out) {
  if (!out) {
    return false;
  }
  memset(out, 0, sizeof(*out));
  if (!rocket_heap_ready(MYNAH_ROCKET_MIN_FETCH_HEAP, MYNAH_ROCKET_MIN_LARGEST_INTERNAL, "rocket")) {
    snprintf(out->error, sizeof(out->error), "low memory");
    pm_rocket_pad_image_release();
    return fill_starship12_demo(out, "low memory");
  }

  char *resp = nullptr;
  size_t rd = 0;
  if (!rocket_http_get_text_alloc(kLl2UpcomingUrl, "application/json", &resp, &rd)) {
    snprintf(out->error, sizeof(out->error), "empty body");
    return fill_starship12_demo(out, "empty body");
  }

  out->count = collect_rll_launches(resp, out->launches, kPmRocketMaxLaunches);
  if (out->count <= 0) {
    out->count = collect_upcoming_launches(resp, out->launches, kPmRocketMaxLaunches);
  }
  free(resp);
  resp = nullptr;
  if (out->count > 0) {
    out->ok = true;
    ESP_LOGI(TAG, "launch clock: %d upcoming (next %s @ %lld)", out->count, out->launches[0].name,
             static_cast<long long>(out->launches[0].net_unix));
    maybe_apply_starship12_webcast(&out->launches[0]);
    if (is_starship_flight_12(out->launches[0])) {
      if (!fetch_starship12_media_timeline(out)) {
        fill_starship12_static_timeline(out);
      }
    }
    if (pm_heap_internal_free() >= MYNAH_ROCKET_DETAIL_MIN_FETCH_HEAP && fetch_launch_detail(&out->launches[0])) {
      maybe_apply_starship12_webcast(&out->launches[0]);
      ESP_LOGI(TAG, "webcast: %s live=%d", out->launches[0].webcast_url, out->launches[0].webcast_live ? 1 : 0);
    }
  } else {
    snprintf(out->error, sizeof(out->error), "no upcoming launch");
    (void)fill_starship12_demo(out, "no upcoming launch");
  }
  return out->ok;
}

static void fetch_mux_ensure(void) {
  if (!s_fetch_mux) {
    s_fetch_mux = xSemaphoreCreateMutex();
  }
}

static void rocket_fetch_task(void *arg) {
  (void)arg;
  for (;;) {
    (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    PmRocketStatus result = {};
    if (pm_resource_acquire(kPmResourceMediaStream, kPmResourceVoice | kPmResourceBustFetch | kPmResourceAnalyzer,
                            "rocket-media")) {
      (void)pm_rocket_fetch(&result);
      pm_resource_release(kPmResourceMediaStream, "rocket-media");
    } else {
      result.ok = false;
      snprintf(result.error, sizeof(result.error), "resource busy");
    }
    fetch_mux_ensure();
    if (!s_fetch_mux || xSemaphoreTake(s_fetch_mux, pdMS_TO_TICKS(1000)) == pdTRUE) {
      s_fetch_result = result;
      if (s_fetch_mux) {
        xSemaphoreGive(s_fetch_mux);
      }
    }
    s_fetch_done = true;
    s_fetch_busy = false;
  }
}

static bool rocket_fetch_task_ensure(void) {
  fetch_mux_ensure();
  if (s_fetch_task) {
    return true;
  }
  return xTaskCreatePinnedToCore(rocket_fetch_task, "rocket_net", kRocketFetchTaskStack, nullptr, 1, &s_fetch_task, 1) ==
         pdPASS;
}

bool pm_rocket_request_fetch(void) {
  if (s_fetch_busy) {
    return true;
  }
  if (!rocket_fetch_task_ensure() || !s_fetch_task) {
    return false;
  }
  s_fetch_done = false;
  s_fetch_busy = true;
  xTaskNotifyGive(s_fetch_task);
  return true;
}

bool pm_rocket_consume_fetch(PmRocketStatus *out) {
  if (!out || !s_fetch_done) {
    return false;
  }
  fetch_mux_ensure();
  if (s_fetch_mux && xSemaphoreTake(s_fetch_mux, pdMS_TO_TICKS(100)) != pdTRUE) {
    return false;
  }
  *out = s_fetch_result;
  if (s_fetch_mux) {
    xSemaphoreGive(s_fetch_mux);
  }
  s_fetch_done = false;
  return true;
}

bool pm_rocket_fetch_busy(void) { return s_fetch_busy; }

uint32_t pm_rocket_fetch_stack_high_water(void) {
  return s_fetch_task ? static_cast<uint32_t>(uxTaskGetStackHighWaterMark(s_fetch_task)) : 0u;
}

bool pm_rocket_release_idle_task(void) {
  if (!s_fetch_task || s_fetch_busy || s_fetch_done) {
    return false;
  }
  TaskHandle_t task = s_fetch_task;
  s_fetch_task = nullptr;
  vTaskDelete(task);
  return true;
}
