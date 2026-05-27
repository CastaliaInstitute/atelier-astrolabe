#include "faces/inq_card/pm_face_inq_card.h"

#include <Arduino_GFX_Library.h>
#include <HTTPClient.h>
#include <PNGdec.h>
#include <WiFiClient.h>
#if !defined(ASTROLABE_WEB_SIM)
#include <WiFiClientSecure.h>
#endif
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "faces/shared/pm_face_draw.h"
#include "pin_config.h"
#include "pm_display.h"
#include "pm_heap.h"
#include "pm_wifi_ntp.h"

namespace {

#ifndef MYNAH_INQ_CARD_BASE_URL
#define MYNAH_INQ_CARD_BASE_URL "https://cards.castalia.institute"
#endif

constexpr int kCx = LCD_WIDTH / 2;
constexpr int kCy = LCD_HEIGHT / 2;
constexpr const char *kBaseUrl = MYNAH_INQ_CARD_BASE_URL;
constexpr uint32_t kFetchTimeoutMs = 30000u;
constexpr uint32_t kMinFetchHeap = 18000u;
constexpr int kMaxJsonBytes = 12288;
constexpr int kMaxImageBytes = 850000;
constexpr int kMaxImageDim = 768;
constexpr uint32_t kTaskStack = 14336u;

struct CardMeta {
  char date[12];
  char title[36];
  char domain[24];
  char token[24];
  char topic[36];
  char focus[36];
  char image[192];
};

TaskHandle_t s_fetch_task = nullptr;
SemaphoreHandle_t s_image_mux = nullptr;
volatile bool s_fetch_busy = false;
volatile bool s_fetch_done = false;
volatile bool s_fetch_ok = false;
CardMeta s_request = {};
CardMeta s_meta = {};
char s_last_error[40] = "";

int s_decoding_w = 0;
int s_decoding_h = 0;
uint16_t *s_decoding_fb = nullptr;
uint8_t *s_decoding_mask = nullptr;
int s_image_w = 0;
int s_image_h = 0;
uint16_t *s_image_fb = nullptr;
uint8_t *s_image_mask = nullptr;
char s_cached_date[12] = "";
PNG *s_png = nullptr;

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

void image_mux_ensure() {
  if (!s_image_mux) {
    s_image_mux = xSemaphoreCreateMutex();
  }
}

bool image_mux_take(uint32_t ms) {
  image_mux_ensure();
  return s_image_mux && xSemaphoreTake(s_image_mux, pdMS_TO_TICKS(ms)) == pdTRUE;
}

void image_mux_give() {
  if (s_image_mux) {
    xSemaphoreGive(s_image_mux);
  }
}

void set_error(const char *msg) {
  if (!msg) {
    s_last_error[0] = '\0';
    return;
  }
  strncpy(s_last_error, msg, sizeof(s_last_error) - 1);
  s_last_error[sizeof(s_last_error) - 1] = '\0';
}

void copy_str(const char *src, char *dst, size_t cap) {
  if (!dst || cap == 0) return;
  if (!src) src = "";
  strncpy(dst, src, cap - 1);
  dst[cap - 1] = '\0';
}

bool json_copy_string(const char *json, const char *key, char *dst, size_t cap) {
  if (!json || !key || !dst || cap == 0) return false;
  char needle[40];
  snprintf(needle, sizeof(needle), "\"%s\"", key);
  const char *p = strstr(json, needle);
  if (!p) return false;
  p = strchr(p + strlen(needle), ':');
  if (!p) return false;
  ++p;
  while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
  if (*p != '"') return false;
  ++p;
  size_t out = 0;
  while (*p && *p != '"' && out + 1 < cap) {
    if (*p == '\\' && p[1]) {
      ++p;
      switch (*p) {
        case 'n':
          dst[out++] = ' ';
          break;
        case 'r':
        case 't':
          dst[out++] = ' ';
          break;
        default:
          dst[out++] = *p;
          break;
      }
      ++p;
      continue;
    }
    dst[out++] = *p++;
  }
  dst[out] = '\0';
  return out > 0;
}

void free_active_image_locked() {
  free(s_image_fb);
  free(s_image_mask);
  s_image_fb = nullptr;
  s_image_mask = nullptr;
  s_image_w = 0;
  s_image_h = 0;
  s_cached_date[0] = '\0';
}

void free_decode_image() {
  free(s_decoding_fb);
  free(s_decoding_mask);
  s_decoding_fb = nullptr;
  s_decoding_mask = nullptr;
  s_decoding_w = 0;
  s_decoding_h = 0;
}

size_t mask_bytes_for(int w, int h) {
  if (w <= 0 || h <= 0) return 0;
  return (static_cast<size_t>(w) * static_cast<size_t>(h) + 7u) / 8u;
}

void mask_set(uint8_t *mask, int idx) {
  if (mask && idx >= 0) {
    mask[idx >> 3] = static_cast<uint8_t>(mask[idx >> 3] | (1u << (idx & 7)));
  }
}

bool mask_get(const uint8_t *mask, int idx) {
  return mask && idx >= 0 && (mask[idx >> 3] & (1u << (idx & 7))) != 0;
}

bool build_meta_url(const char *date, char *url, size_t cap) {
  if (!date || !url || cap == 0 || strlen(date) != 10) return false;
  const int n = snprintf(url, cap, "%s/card-of-the-day/%s.json", kBaseUrl, date);
  return n > 0 && static_cast<size_t>(n) < cap;
}

bool png_ensure() {
  if (!s_png) {
    s_png = new PNG();
  }
  if (!s_png) {
    set_error("png alloc");
    return false;
  }
  return true;
}

bool read_http_body(HTTPClient &http, uint8_t **out_buf, size_t *out_len, int max_bytes) {
  if (!out_buf || !out_len) return false;
  *out_buf = nullptr;
  *out_len = 0;
  const int len = http.getSize();
  if (len <= 0 || len > max_bytes) {
    return false;
  }
  uint8_t *buf = static_cast<uint8_t *>(pm_heap_alloc_response(static_cast<size_t>(len) + 1u));
  if (!buf) {
    return false;
  }
  WiFiClient *stream = http.getStreamPtr();
  size_t rd = 0;
  const uint32_t deadline = millis() + kFetchTimeoutMs;
  while (rd < static_cast<size_t>(len)) {
    if (stream && stream->available() > 0) {
      const int n = stream->readBytes(buf + rd, static_cast<size_t>(len) - rd);
      if (n > 0) {
        rd += static_cast<size_t>(n);
        continue;
      }
    }
    if (!http.connected() && (!stream || stream->available() == 0)) break;
    if (static_cast<int32_t>(millis() - deadline) >= 0) break;
    yield();
    delay(1);
  }
  if (rd == 0 || rd > static_cast<size_t>(max_bytes)) {
    free(buf);
    return false;
  }
  buf[rd] = 0;
  *out_buf = buf;
  *out_len = rd;
  return true;
}

bool fetch_card_meta(CardMeta *meta) {
  if (!meta || !pm_wifi_connected()) {
    set_error("no wifi");
    return false;
  }
  char url[160];
  if (!build_meta_url(meta->date, url, sizeof(url))) {
    set_error("bad date");
    return false;
  }
#if defined(ASTROLABE_WEB_SIM)
  WiFiClient client;
#else
  WiFiClientSecure client;
  client.setInsecure();
#endif
  HTTPClient http;
  http.setTimeout(kFetchTimeoutMs);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.addHeader("Accept", "application/json");
  if (!http.begin(client, url)) {
    set_error("json begin");
    return false;
  }
  const int code = http.GET();
  if (code != 200) {
    snprintf(s_last_error, sizeof(s_last_error), "json %d", code);
    http.end();
    return false;
  }
  uint8_t *body = nullptr;
  size_t body_len = 0;
  if (!read_http_body(http, &body, &body_len, kMaxJsonBytes)) {
    http.end();
    set_error("json body");
    return false;
  }
  http.end();

  const char *json = reinterpret_cast<const char *>(body);
  json_copy_string(json, "date", meta->date, sizeof(meta->date));
  if (!json_copy_string(json, "title", meta->title, sizeof(meta->title))) {
    copy_str("iNQ Card", meta->title, sizeof(meta->title));
  }
  json_copy_string(json, "domain", meta->domain, sizeof(meta->domain));
  json_copy_string(json, "token", meta->token, sizeof(meta->token));
  json_copy_string(json, "topic", meta->topic, sizeof(meta->topic));
  if (!json_copy_string(json, "focus", meta->focus, sizeof(meta->focus))) {
    json_copy_string(json, "prompt", meta->focus, sizeof(meta->focus));
  }
  json_copy_string(json, "image", meta->image, sizeof(meta->image));
  free(body);
  if (meta->image[0] == '\0') {
    set_error("no image");
    return false;
  }
  return true;
}

bool download_png(const char *url, uint8_t **out_buf, size_t *out_len) {
  if (!url || !out_buf || !out_len) return false;
  *out_buf = nullptr;
  *out_len = 0;
  if (!pm_wifi_connected()) {
    set_error("no wifi");
    return false;
  }
  if (pm_heap_internal_free() < kMinFetchHeap) {
    set_error("low memory");
    return false;
  }
#if defined(ASTROLABE_WEB_SIM)
  WiFiClient client;
#else
  WiFiClientSecure client;
  client.setInsecure();
#endif
  HTTPClient http;
  http.setTimeout(kFetchTimeoutMs);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.addHeader("Accept", "image/png,image/*;q=0.8,*/*;q=0.1");
  http.addHeader("User-Agent", "Astrolabe/1.0");
  if (!http.begin(client, url)) {
    set_error("img begin");
    return false;
  }
  const int code = http.GET();
  if (code != 200) {
    snprintf(s_last_error, sizeof(s_last_error), "image %d", code);
    http.end();
    return false;
  }
  const bool ok = read_http_body(http, out_buf, out_len, kMaxImageBytes);
  http.end();
  if (!ok) {
    set_error("image body");
  }
  return ok;
}

int png_draw(PNGDRAW *pDraw) {
  if (!pDraw || !s_decoding_fb || s_decoding_w <= 0 || s_decoding_h <= 0 || pDraw->y < 0 ||
      pDraw->y >= s_decoding_h) {
    return 0;
  }
  uint16_t *dst = s_decoding_fb + pDraw->y * s_decoding_w;
  s_png->getLineAsRGB565(pDraw, dst, PNG_RGB565_BIG_ENDIAN, 0xffffffff);
  if (s_decoding_mask && pDraw->iWidth > 0) {
    uint8_t alpha_mask[(kMaxImageDim + 7) / 8] = {};
    if (s_png->getAlphaMask(pDraw, alpha_mask, 8)) {
      for (int x = 0; x < pDraw->iWidth && x < s_decoding_w; ++x) {
        if ((alpha_mask[x >> 3] & (0x80u >> (x & 7))) != 0) {
          mask_set(s_decoding_mask, pDraw->y * s_decoding_w + x);
        }
      }
    } else {
      for (int x = 0; x < pDraw->iWidth && x < s_decoding_w; ++x) {
        mask_set(s_decoding_mask, pDraw->y * s_decoding_w + x);
      }
    }
  }
  return 1;
}

bool decode_png(uint8_t *data, size_t len, const char *date) {
  free_decode_image();
  if (!png_ensure() || !data || len == 0 || s_png->openRAM(data, static_cast<int>(len), png_draw) != PNG_SUCCESS) {
    set_error("png open");
    return false;
  }
  const int w = s_png->getWidth();
  const int h = s_png->getHeight();
  if (w <= 0 || h <= 0 || w > kMaxImageDim || h > kMaxImageDim) {
    s_png->close();
    set_error("png size");
    return false;
  }
  const size_t px = static_cast<size_t>(w) * static_cast<size_t>(h);
  s_decoding_fb = static_cast<uint16_t *>(pm_heap_alloc_response(px * sizeof(uint16_t)));
  s_decoding_mask = static_cast<uint8_t *>(pm_heap_alloc_response(mask_bytes_for(w, h)));
  if (!s_decoding_fb || !s_decoding_mask) {
    s_png->close();
    free_decode_image();
    set_error("alloc img");
    return false;
  }
  s_decoding_w = w;
  s_decoding_h = h;
  memset(s_decoding_fb, 0, px * sizeof(uint16_t));
  memset(s_decoding_mask, 0, mask_bytes_for(w, h));
  const int rc = s_png->decode(nullptr, 0);
  s_png->close();
  if (rc != PNG_SUCCESS) {
    free_decode_image();
    set_error("png decode");
    return false;
  }
  if (!image_mux_take(3000)) {
    free_decode_image();
    set_error("image lock");
    return false;
  }
  free_active_image_locked();
  s_image_fb = s_decoding_fb;
  s_image_mask = s_decoding_mask;
  s_image_w = s_decoding_w;
  s_image_h = s_decoding_h;
  copy_str(date, s_cached_date, sizeof(s_cached_date));
  s_decoding_fb = nullptr;
  s_decoding_mask = nullptr;
  s_decoding_w = 0;
  s_decoding_h = 0;
  image_mux_give();
  set_error(nullptr);
  return true;
}

bool fetch_card_inner(CardMeta *meta) {
  if (!fetch_card_meta(meta)) {
    return false;
  }
  uint8_t *png = nullptr;
  size_t png_len = 0;
  if (!download_png(meta->image, &png, &png_len)) {
    return false;
  }
  const bool ok = decode_png(png, png_len, meta->date);
  free(png);
  if (ok) {
    s_meta = *meta;
  }
  Serial.printf("inq-card: %s %s %s (%u B)\n", ok ? "cached" : "decode failed", meta->date, meta->title,
                static_cast<unsigned>(png_len));
  return ok;
}

void fetch_task(void *arg) {
  (void)arg;
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    CardMeta meta = s_request;
    s_fetch_ok = fetch_card_inner(&meta);
    s_fetch_done = true;
    s_fetch_busy = false;
  }
}

void fetch_task_ensure() {
  if (!s_fetch_task) {
    const BaseType_t ok = xTaskCreatePinnedToCore(fetch_task, "inq_card", kTaskStack, nullptr, 1, &s_fetch_task, 1);
    if (ok != pdPASS) {
      s_fetch_task = nullptr;
      set_error("task alloc");
    }
  }
}

void request_card(const char *date) {
  if (!date || s_fetch_busy || strcmp(s_cached_date, date) == 0 || !pm_wifi_connected()) return;
  fetch_task_ensure();
  if (!s_fetch_task) {
    set_error("task");
    return;
  }
  memset(&s_request, 0, sizeof(s_request));
  copy_str(date, s_request.date, sizeof(s_request.date));
  s_fetch_done = false;
  s_fetch_ok = false;
  s_fetch_busy = true;
  xTaskNotify(s_fetch_task, 1, eSetBits);
}

bool draw_cached_image(const char *date, int cx, int cy, int max_w, int max_h) {
  if (!date || !image_mux_take(25)) return false;
  if (strcmp(s_cached_date, date) != 0 || !s_image_fb || !s_image_mask || s_image_w <= 0 || s_image_h <= 0) {
    image_mux_give();
    return false;
  }
  const float scale = fminf(static_cast<float>(max_w) / static_cast<float>(s_image_w),
                           static_cast<float>(max_h) / static_cast<float>(s_image_h));
  const int dw = max(1, static_cast<int>(floorf(static_cast<float>(s_image_w) * scale)));
  const int dh = max(1, static_cast<int>(floorf(static_cast<float>(s_image_h) * scale)));
  const int x0 = cx - dw / 2;
  const int y0 = cy - dh / 2;
  for (int y = 0; y < dh; ++y) {
    const int sy = min(s_image_h - 1, (y * s_image_h) / dh);
    const int yy = y0 + y;
    if (yy < 0 || yy >= LCD_HEIGHT) continue;
    for (int x = 0; x < dw; ++x) {
      const int sx = min(s_image_w - 1, (x * s_image_w) / dw);
      const int xx = x0 + x;
      if (xx < 0 || xx >= LCD_WIDTH) continue;
      const int src = sy * s_image_w + sx;
      if (!mask_get(s_image_mask, src)) continue;
      pm_gfx->writePixel(xx, yy, s_image_fb[src]);
    }
  }
  image_mux_give();
  return true;
}

bool date_from_tm(const struct tm *tm_local, bool valid_local, char *date, size_t cap) {
  if (!valid_local || !tm_local || !date || cap < 11) return false;
  const int n = snprintf(date, cap, "%04d-%02d-%02d", tm_local->tm_year + 1900, tm_local->tm_mon + 1,
                         tm_local->tm_mday);
  return n == 10;
}

void draw_placeholder(uint16_t bg, uint16_t ink, uint16_t accent, const char *msg) {
  pm_gfx->fillCircle(kCx, kCy, 154, pm_gfx->color565(250, 247, 239));
  pm_gfx->drawCircle(kCx, kCy, 154, blend565(bg, accent, 0.65f));
  pm_gfx->drawCircle(kCx, kCy, 132, blend565(bg, ink, 0.24f));
  pm_gfx->drawFastHLine(kCx - 86, kCy - 30, 172, blend565(bg, ink, 0.5f));
  pm_gfx->drawFastHLine(kCx - 86, kCy + 30, 172, blend565(bg, ink, 0.5f));
  pm_gfx->drawCircle(kCx, kCy, 54, accent);
  pm_face_draw_centered_line(msg, kCy - 8, ink, 1, 1);
}

}  // namespace

const char *pm_face_inq_card_title(void) { return s_meta.title; }

const char *pm_face_inq_card_date(void) { return s_meta.date; }

void pm_face_inq_card_draw(const struct tm *tm_local, bool valid_local) {
  char date[12] = "";
  const bool have_date = date_from_tm(tm_local, valid_local, date, sizeof(date));
  if (have_date) {
    request_card(date);
  }

  const uint16_t c_bg = pm_gfx->color565(21, 20, 18);
  const uint16_t c_card = pm_gfx->color565(250, 247, 239);
  const uint16_t c_ink = pm_gfx->color565(42, 37, 32);
  const uint16_t c_dim = pm_gfx->color565(139, 125, 107);
  const uint16_t c_accent = pm_gfx->color565(184, 134, 11);
  pm_gfx->fillScreen(c_bg);

  const bool image_drawn = have_date && draw_cached_image(date, kCx, kCy - 8, 356, 356);
  if (!image_drawn) {
    const char *msg = !have_date ? "sync time" : (s_fetch_busy ? "fetching" : (s_last_error[0] ? s_last_error : "card"));
    draw_placeholder(c_bg, c_ink, c_accent, msg);
  }

  pm_gfx->fillRect(0, 0, LCD_WIDTH, 62, c_bg);
  pm_gfx->fillRect(0, 340, LCD_WIDTH, 126, c_bg);
  pm_gfx->drawFastHLine(0, 62, LCD_WIDTH, blend565(c_bg, c_accent, 0.55f));
  pm_gfx->drawFastHLine(0, 340, LCD_WIDTH, blend565(c_bg, c_accent, 0.55f));

  pm_face_draw_centered_line("iNQ card", 36, c_accent, 2, 2);
  const char *title = (s_meta.title[0] && (!have_date || strcmp(s_meta.date, date) == 0)) ? s_meta.title : "Card of the Day";
  const char *focus = (s_meta.focus[0] && (!have_date || strcmp(s_meta.date, date) == 0)) ? s_meta.focus : "";
  pm_face_draw_centered_line(title, 356, c_card, 2, 2);
  pm_face_draw_centered_line(focus[0] ? focus : (have_date ? date : "waiting for clock"), 390, c_dim, 1, 1);
  pm_face_draw_centered_line(s_fetch_busy ? "loading image" : "from cards", 420, c_dim, 1, 1);
}
