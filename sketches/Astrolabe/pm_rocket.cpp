#include "pm_rocket.h"

#include <Arduino_GFX_Library.h>
#include <HTTPClient.h>
#include <JPEGDEC.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <time.h>

#include "esp_log.h"
#include "pin_config.h"
#include "pm_config.h"
#include "pm_display.h"
#include "pm_heap.h"

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

static bool http_download_binary(const char *url, uint8_t **out_buf, size_t *out_len) {
  if (!url || !url[0] || !out_buf || !out_len) {
    return false;
  }
  *out_buf = nullptr;
  *out_len = 0;
  if (!pm_heap_tls_ready(MYNAH_ROCKET_MIN_FETCH_HEAP, "rocket image")) {
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(MYNAH_ROCKET_HTTP_MS);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.addHeader("User-Agent", kLl2UserAgent);
  if (!http.begin(client, url)) {
    return false;
  }

  const int code = http.GET();
  const int len = http.getSize();
  if (code != 200 || len <= 0 || len > MYNAH_ROCKET_IMAGE_MAX_BYTES) {
    ESP_LOGW(TAG, "image GET %d len %d", code, len);
    http.end();
    return false;
  }

  uint8_t *buf = static_cast<uint8_t *>(pm_heap_alloc_response(static_cast<size_t>(len)));
  if (!buf) {
    http.end();
    return false;
  }

  WiFiClient *stream = http.getStreamPtr();
  size_t rd = 0;
  const uint32_t deadline = millis() + MYNAH_ROCKET_HTTP_MS;
  while (rd < static_cast<size_t>(len)) {
    if (stream->available() > 0) {
      const int n = stream->readBytes(buf + rd, static_cast<size_t>(len) - rd);
      if (n > 0) {
        rd += static_cast<size_t>(n);
        continue;
      }
    }
    if (!http.connected() && stream->available() == 0) {
      break;
    }
    if (static_cast<int32_t>(millis() - deadline) >= 0) {
      break;
    }
    yield();
    delay(1);
  }
  http.end();
  if (rd < 8) {
    free(buf);
    return false;
  }
  *out_buf = buf;
  *out_len = rd;
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
  if (!http_download_binary(url, &img, &img_len) || !img) {
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

static bool read_http_body(HTTPClient &http, int streamLen, char **out_resp, size_t *out_rd) {
  if (!out_resp || !out_rd || streamLen <= 0) {
    return false;
  }
  char *resp = static_cast<char *>(pm_heap_alloc_response(static_cast<size_t>(streamLen) + 1));
  if (!resp) {
    return false;
  }

  WiFiClient *stream = http.getStreamPtr();
  size_t rd = 0;
  const uint32_t deadline = millis() + MYNAH_ROCKET_HTTP_MS;
  while (rd < static_cast<size_t>(streamLen)) {
    if (stream->available() > 0) {
      const int n = stream->readBytes(resp + rd, static_cast<size_t>(streamLen) - rd);
      if (n > 0) {
        rd += static_cast<size_t>(n);
        continue;
      }
    }
    if (static_cast<int32_t>(millis() - deadline) >= 0) {
      break;
    }
    delay(2);
  }
  resp[rd] = '\0';
  *out_resp = resp;
  *out_rd = rd;
  return rd > 0;
}

static bool fetch_launch_detail(PmRocketLaunch *launch) {
  if (!launch || !launch->valid || launch->id[0] == '\0') {
    return false;
  }

  char url[120];
  snprintf(url, sizeof(url), "https://ll.thespacedevs.com/2.2.0/launch/%s/", launch->id);
  if (!pm_heap_tls_ready(MYNAH_ROCKET_MIN_FETCH_HEAP, "rocket detail")) {
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(MYNAH_ROCKET_HTTP_MS);
  http.addHeader("User-Agent", kLl2UserAgent);
  if (!http.begin(client, url)) {
    return false;
  }

  const int code = http.GET();
  const int streamLen = http.getSize();
  if (code != 200 || streamLen <= 0 || streamLen > MYNAH_ROCKET_MAX_BYTES) {
    http.end();
    return false;
  }

  char *resp = nullptr;
  size_t rd = 0;
  if (!read_http_body(http, streamLen, &resp, &rd)) {
    http.end();
    return false;
  }
  http.end();

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
  if (!pm_heap_tls_ready(MYNAH_ROCKET_MIN_FETCH_HEAP, "rocket")) {
    snprintf(out->error, sizeof(out->error), "low memory");
    pm_rocket_pad_image_release();
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(MYNAH_ROCKET_HTTP_MS);
  http.addHeader("User-Agent", kLl2UserAgent);
  if (!http.begin(client, kLl2UpcomingUrl)) {
    snprintf(out->error, sizeof(out->error), "HTTP begin failed");
    return false;
  }

  const int code = http.GET();
  const int streamLen = http.getSize();
  if (code != 200 || streamLen <= 0 || streamLen > MYNAH_ROCKET_MAX_BYTES) {
    ESP_LOGW(TAG, "LL2 HTTP %d len %d", code, streamLen);
    snprintf(out->error, sizeof(out->error), "HTTP %d", code);
    http.end();
    return false;
  }

  char *resp = static_cast<char *>(pm_heap_alloc_response(static_cast<size_t>(streamLen) + 1));
  if (!resp) {
    http.end();
    snprintf(out->error, sizeof(out->error), "alloc");
    return false;
  }

  WiFiClient *stream = http.getStreamPtr();
  size_t rd = 0;
  const uint32_t deadline = millis() + MYNAH_ROCKET_HTTP_MS;
  while (rd < static_cast<size_t>(streamLen)) {
    if (stream->available() > 0) {
      const int n = stream->readBytes(resp + rd, static_cast<size_t>(streamLen) - rd);
      if (n > 0) {
        rd += static_cast<size_t>(n);
        continue;
      }
    }
    if (static_cast<int32_t>(millis() - deadline) >= 0) {
      break;
    }
    delay(2);
  }
  resp[rd] = '\0';
  http.end();

  if (rd == 0) {
    free(resp);
    snprintf(out->error, sizeof(out->error), "empty body");
    return false;
  }

  out->count = collect_rll_launches(resp, out->launches, kPmRocketMaxLaunches);
  if (out->count <= 0) {
    out->count = collect_upcoming_launches(resp, out->launches, kPmRocketMaxLaunches);
  }
  if (out->count > 0) {
    out->ok = true;
    ESP_LOGI(TAG, "launch clock: %d upcoming (next %s @ %lld)", out->count, out->launches[0].name,
             static_cast<long long>(out->launches[0].net_unix));
    if (pm_heap_internal_free() >= 140000u && fetch_launch_detail(&out->launches[0])) {
      ESP_LOGI(TAG, "webcast: %s live=%d", out->launches[0].webcast_url, out->launches[0].webcast_live ? 1 : 0);
    }
  } else {
    snprintf(out->error, sizeof(out->error), "no upcoming launch");
  }
  free(resp);
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
    (void)pm_rocket_fetch(&result);
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
