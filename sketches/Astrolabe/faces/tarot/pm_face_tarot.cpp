#include "faces/tarot/pm_face_tarot.h"

#include <Arduino_GFX_Library.h>
#include <HTTPClient.h>
#include <JPEGDEC.h>
#include <LittleFS.h>
#include <PNGdec.h>
#include <WiFiClient.h>
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

constexpr int kCardCount = 22;
constexpr int kCx = LCD_WIDTH / 2;
constexpr int kCy = LCD_HEIGHT / 2;
constexpr const char *kManifestUrl = "http://tarot.castalia.institute/assets/major/manifest.json";
constexpr const char *kAssetBaseUrl = "http://tarot.castalia.institute/assets/major/half";
constexpr uint32_t kTarotFetchTimeoutMs = 30000u;
constexpr uint32_t kTarotMinFetchHeap = 18000u;
constexpr int kTarotMaxImageBytes = 390000;
constexpr int kTarotMaxFlashImageBytes = 96 * 1024;
constexpr int kTarotMaxImageDim = LCD_WIDTH;
constexpr int kTarotMaxDecodeLine = 512;
constexpr uint32_t kTarotTaskStack = 12288u;

struct TarotCard {
  const char *title;
  const char *glyph;
  const char *theme;
  const char *slug;
  uint8_t r;
  uint8_t g;
  uint8_t b;
};

const TarotCard kCards[kCardCount] = {
    {"The Fool", "0", "begin", "fool", 244, 204, 90},
    {"The Magician", "I", "will", "magician", 220, 70, 64},
    {"The High Priestess", "II", "veil", "priestess", 78, 116, 210},
    {"The Empress", "III", "bloom", "empress", 88, 170, 98},
    {"The Emperor", "IV", "order", "emperor", 196, 82, 54},
    {"The Hierophant", "V", "rite", "hierophant", 190, 170, 108},
    {"The Lovers", "VI", "choice", "lovers", 225, 112, 142},
    {"The Chariot", "VII", "drive", "chariot", 80, 132, 210},
    {"Strength", "VIII", "gentle", "strength", 238, 170, 76},
    {"The Hermit", "IX", "lamp", "hermit", 170, 186, 205},
    {"Wheel of Fortune", "X", "turn", "fortune", 214, 174, 72},
    {"Justice", "XI", "balance", "justice", 190, 82, 86},
    {"The Hanged Man", "XII", "pause", "hanged", 92, 166, 190},
    {"Death", "XIII", "change", "death", 210, 210, 210},
    {"Temperance", "XIV", "blend", "temperance", 116, 184, 164},
    {"The Devil", "XV", "chain", "devil", 174, 64, 72},
    {"The Tower", "XVI", "break", "tower", 230, 144, 64},
    {"The Star", "XVII", "hope", "star", 116, 174, 226},
    {"The Moon", "XVIII", "dream", "moon", 150, 150, 218},
    {"The Sun", "XIX", "joy", "sun", 248, 204, 74},
    {"Judgement", "XX", "call", "judgement", 214, 130, 92},
    {"The World", "XXI", "whole", "world", 116, 190, 142},
};

int s_selected = -1;
TaskHandle_t s_fetch_task = nullptr;
SemaphoreHandle_t s_image_mux = nullptr;
volatile bool s_fetch_busy = false;
volatile bool s_fetch_done = false;
volatile bool s_fetch_ok = false;
int s_request_idx = -1;
int s_cached_idx = -1;
int s_decoding_w = 0;
int s_decoding_h = 0;
uint16_t *s_decoding_fb = nullptr;
uint8_t *s_decoding_mask = nullptr;
int s_image_w = 0;
int s_image_h = 0;
uint16_t *s_image_fb = nullptr;
uint8_t *s_image_mask = nullptr;
char s_last_error[40] = "";
uint16_t s_decode_line[kTarotMaxDecodeLine];
PNG s_png;
JPEGDEC s_jpg;
bool s_fs_checked = false;
bool s_fs_ready = false;

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

void free_active_image_locked() {
  free(s_image_fb);
  free(s_image_mask);
  s_image_fb = nullptr;
  s_image_mask = nullptr;
  s_image_w = 0;
  s_image_h = 0;
  s_cached_idx = -1;
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
  if (w <= 0 || h <= 0) {
    return 0;
  }
  return (static_cast<size_t>(w) * static_cast<size_t>(h) + 7u) / 8u;
}

void mask_set(uint8_t *mask, int idx) {
  if (!mask || idx < 0) {
    return;
  }
  mask[idx >> 3] = static_cast<uint8_t>(mask[idx >> 3] | (1u << (idx & 7)));
}

bool mask_get(const uint8_t *mask, int idx) {
  if (!mask || idx < 0) {
    return false;
  }
  return (mask[idx >> 3] & (1u << (idx & 7))) != 0;
}

bool build_card_url(int idx, char *url, size_t cap) {
  if (idx < 0 || idx >= kCardCount || !url || cap == 0) {
    return false;
  }
  const int n = snprintf(url, cap, "%s/%02d-%s.png", kAssetBaseUrl, idx, kCards[idx].slug);
  return n > 0 && static_cast<size_t>(n) < cap;
}

bool build_flash_card_path(int idx, char *path, size_t cap) {
  if (idx < 0 || idx >= kCardCount || !path || cap == 0) {
    return false;
  }
  const int n = snprintf(path, cap, "/tarot/%02d-%s.jpg", idx, kCards[idx].slug);
  return n > 0 && static_cast<size_t>(n) < cap;
}

bool tarot_fs_ready() {
  if (!s_fs_checked) {
    s_fs_ready = LittleFS.begin(false);
    s_fs_checked = true;
  }
  return s_fs_ready;
}

bool download_card_png(const char *url, uint8_t **out_buf, size_t *out_len) {
  if (!url || !out_buf || !out_len) {
    return false;
  }
  *out_buf = nullptr;
  *out_len = 0;
  if (!pm_wifi_connected()) {
    set_error("no wifi");
    return false;
  }
  if (pm_heap_internal_free() < kTarotMinFetchHeap) {
    set_error("low memory");
    return false;
  }

  WiFiClient client;
  HTTPClient http;
  http.setTimeout(kTarotFetchTimeoutMs);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.addHeader("Accept", "image/png,image/*;q=0.8,*/*;q=0.1");
  http.addHeader("User-Agent", "Astrolabe/1.0");
  if (!http.begin(client, url)) {
    set_error("http begin");
    return false;
  }
  const int code = http.GET();
  const int len = http.getSize();
  if (code != 200 || len <= 0 || len > kTarotMaxImageBytes) {
    char err[32];
    snprintf(err, sizeof(err), "HTTP %d", code);
    set_error(err);
    Serial.printf("tarot: image GET %d len %d\n", code, len);
    http.end();
    return false;
  }
  uint8_t *buf = static_cast<uint8_t *>(pm_heap_alloc_response(static_cast<size_t>(len)));
  if (!buf) {
    set_error("alloc png");
    http.end();
    return false;
  }
  WiFiClient *stream = http.getStreamPtr();
  size_t rd = 0;
  const uint32_t deadline = millis() + kTarotFetchTimeoutMs;
  while (rd < static_cast<size_t>(len)) {
    if (stream && stream->available() > 0) {
      const int n = stream->readBytes(buf + rd, static_cast<size_t>(len) - rd);
      if (n > 0) {
        rd += static_cast<size_t>(n);
        continue;
      }
    }
    if (!http.connected() && (!stream || stream->available() == 0)) {
      break;
    }
    if (static_cast<int32_t>(millis() - deadline) >= 0) {
      break;
    }
    yield();
    delay(1);
  }
  http.end();
  if (rd < 8 || rd < static_cast<size_t>(len)) {
    free(buf);
    set_error("short png");
    return false;
  }
  *out_buf = buf;
  *out_len = rd;
  return true;
}

int tarot_png_draw(PNGDRAW *pDraw) {
  if (!pDraw || !s_decoding_fb || s_decoding_w <= 0 || s_decoding_h <= 0 || pDraw->y < 0 || pDraw->y >= s_decoding_h) {
    return 0;
  }
  if (pDraw->iWidth <= 0 || pDraw->iWidth > kTarotMaxDecodeLine) {
    return 0;
  }
  s_png.getLineAsRGB565(pDraw, s_decode_line, PNG_RGB565_BIG_ENDIAN, 0xffffffff);
  const int copy_w = min(pDraw->iWidth, s_decoding_w);
  uint16_t *dst = s_decoding_fb + pDraw->y * s_decoding_w;
  memcpy(dst, s_decode_line, static_cast<size_t>(copy_w) * sizeof(uint16_t));
  if (s_decoding_mask && pDraw->iWidth > 0) {
    uint8_t alpha_mask[(kTarotMaxImageDim + 7) / 8] = {};
    if (s_png.getAlphaMask(pDraw, alpha_mask, 8)) {
      for (int x = 0; x < pDraw->iWidth && x < s_decoding_w; ++x) {
        if ((alpha_mask[x >> 3] & (0x80u >> (x & 7))) != 0) {
          mask_set(s_decoding_mask, pDraw->y * s_decoding_w + x);
        }
      }
    }
  }
  return 1;
}

bool decode_card_png(uint8_t *data, size_t len, int idx) {
  free_decode_image();
  if (!data || len == 0 || s_png.openRAM(data, static_cast<int>(len), tarot_png_draw) != PNG_SUCCESS) {
    set_error("png open");
    return false;
  }
  const int w = s_png.getWidth();
  const int h = s_png.getHeight();
  if (w <= 0 || h <= 0 || w > kTarotMaxImageDim || h > kTarotMaxImageDim) {
    s_png.close();
    set_error("png size");
    return false;
  }
  const size_t px = static_cast<size_t>(w) * static_cast<size_t>(h);
  s_decoding_fb = static_cast<uint16_t *>(pm_heap_alloc_response(px * sizeof(uint16_t)));
  if (!s_decoding_fb) {
    s_png.close();
    set_error("alloc fb");
    return false;
  }
  s_decoding_w = w;
  s_decoding_h = h;
  memset(s_decoding_fb, 0, px * sizeof(uint16_t));
  const int rc = s_png.decode(nullptr, 0);
  s_png.close();
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
  s_cached_idx = idx;
  s_decoding_fb = nullptr;
  s_decoding_mask = nullptr;
  s_decoding_w = 0;
  s_decoding_h = 0;
  image_mux_give();
  set_error(nullptr);
  return true;
}

int tarot_jpeg_draw(JPEGDRAW *pDraw) {
  if (!pDraw || !s_decoding_fb || s_decoding_w <= 0 || s_decoding_h <= 0) {
    return 0;
  }
  if (pDraw->x < 0 || pDraw->y < 0 || pDraw->x + pDraw->iWidth > s_decoding_w ||
      pDraw->y + pDraw->iHeight > s_decoding_h) {
    return 0;
  }
  for (int row = 0; row < pDraw->iHeight; ++row) {
    uint16_t *dst = s_decoding_fb + (pDraw->y + row) * s_decoding_w + pDraw->x;
    const uint16_t *src = pDraw->pPixels + row * pDraw->iWidth;
    memcpy(dst, src, static_cast<size_t>(pDraw->iWidth) * sizeof(uint16_t));
  }
  return 1;
}

bool decode_card_jpeg(uint8_t *data, size_t len, int idx) {
  free_decode_image();
  s_jpg.close();
  if (!data || len == 0 || s_jpg.openRAM(data, static_cast<int>(len), tarot_jpeg_draw) != 1) {
    set_error("jpg open");
    return false;
  }
  const int w = s_jpg.getWidth();
  const int h = s_jpg.getHeight();
  if (w <= 0 || h <= 0 || w > kTarotMaxImageDim || h > kTarotMaxImageDim) {
    s_jpg.close();
    set_error("jpg size");
    return false;
  }
  const size_t px = static_cast<size_t>(w) * static_cast<size_t>(h);
  s_decoding_fb = static_cast<uint16_t *>(pm_heap_alloc_response(px * sizeof(uint16_t)));
  if (!s_decoding_fb) {
    s_jpg.close();
    set_error("alloc jpg");
    return false;
  }
  s_decoding_w = w;
  s_decoding_h = h;
  memset(s_decoding_fb, 0, px * sizeof(uint16_t));
  s_jpg.setPixelType(RGB565_BIG_ENDIAN);
  if (s_jpg.decode(0, 0, 0) != 1) {
    s_jpg.close();
    free_decode_image();
    set_error("jpg decode");
    return false;
  }
  s_jpg.close();
  if (!image_mux_take(3000)) {
    free_decode_image();
    set_error("image lock");
    return false;
  }
  free_active_image_locked();
  s_image_fb = s_decoding_fb;
  s_image_mask = nullptr;
  s_image_w = s_decoding_w;
  s_image_h = s_decoding_h;
  s_cached_idx = idx;
  s_decoding_fb = nullptr;
  s_decoding_w = 0;
  s_decoding_h = 0;
  image_mux_give();
  set_error(nullptr);
  return true;
}

bool load_flash_card_image(int idx) {
  if (idx < 0 || idx >= kCardCount || s_cached_idx == idx) {
    return s_cached_idx == idx;
  }
  if (!tarot_fs_ready()) {
    set_error("no flash fs");
    return false;
  }
  char path[48];
  if (!build_flash_card_path(idx, path, sizeof(path))) {
    set_error("bad path");
    return false;
  }
  File f = LittleFS.open(path, FILE_READ);
  if (!f) {
    set_error("no flash card");
    return false;
  }
  const size_t len = f.size();
  if (len < 8 || len > kTarotMaxFlashImageBytes) {
    f.close();
    set_error("card size");
    return false;
  }
  uint8_t *jpg = static_cast<uint8_t *>(pm_heap_alloc_response(len));
  if (!jpg) {
    f.close();
    set_error("alloc card");
    return false;
  }
  const size_t rd = f.readBytes(reinterpret_cast<char *>(jpg), len);
  f.close();
  if (rd != len) {
    free(jpg);
    set_error("read card");
    return false;
  }
  const bool ok = decode_card_jpeg(jpg, len, idx);
  free(jpg);
  if (ok) {
    Serial.printf("tarot: flash cached %02d %s (%u B)\n", idx, kCards[idx].slug, static_cast<unsigned>(len));
  }
  return ok;
}

bool fetch_card_inner(int idx) {
  char url[160];
  if (!build_card_url(idx, url, sizeof(url))) {
    set_error("bad url");
    return false;
  }
  uint8_t *png = nullptr;
  size_t png_len = 0;
  if (!download_card_png(url, &png, &png_len)) {
    return false;
  }
  const bool ok = decode_card_png(png, png_len, idx);
  free(png);
  Serial.printf("tarot: %s %02d %s (%u B)\n", ok ? "cached" : "decode failed", idx, kCards[idx].slug,
                static_cast<unsigned>(png_len));
  return ok;
}

void tarot_fetch_task(void *arg) {
  (void)arg;
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    const int idx = s_request_idx;
    s_fetch_ok = fetch_card_inner(idx);
    s_fetch_done = true;
    s_fetch_busy = false;
  }
}

void fetch_task_ensure() {
  if (!s_fetch_task) {
    const BaseType_t ok =
        xTaskCreatePinnedToCore(tarot_fetch_task, "tarot_img", kTarotTaskStack, nullptr, 1, &s_fetch_task, 1);
    if (ok != pdPASS) {
      s_fetch_task = nullptr;
      set_error("task alloc");
    }
  }
}

void request_card_image(int idx) {
  if (idx < 0 || idx >= kCardCount || s_cached_idx == idx || s_fetch_busy || !pm_wifi_connected()) {
    return;
  }
  fetch_task_ensure();
  if (!s_fetch_task) {
    set_error("task");
    return;
  }
  s_request_idx = idx;
  s_fetch_done = false;
  s_fetch_ok = false;
  s_fetch_busy = true;
  xTaskNotify(s_fetch_task, 1, eSetBits);
}

bool draw_cached_card_image(int idx, int cx, int cy) {
  if (idx < 0 || !image_mux_take(25)) {
    return false;
  }
  if (s_cached_idx != idx || !s_image_fb || s_image_w <= 0 || s_image_h <= 0) {
    image_mux_give();
    return false;
  }
  const int scale = (s_image_w == LCD_WIDTH && s_image_h == LCD_HEIGHT) ? 1
                    : (s_image_w * 2 == LCD_WIDTH && s_image_h * 2 == LCD_HEIGHT) ? 2
                                                                                   : 1;
  const int draw_w = s_image_w * scale;
  const int draw_h = s_image_h * scale;
  const int x0 = cx - draw_w / 2;
  const int y0 = cy - draw_h / 2;
  for (int y = 0; y < s_image_h; ++y) {
    for (int x = 0; x < s_image_w; ++x) {
      if (s_image_mask && !mask_get(s_image_mask, y * s_image_w + x)) {
        continue;
      }
      const uint16_t px = s_image_fb[y * s_image_w + x];
      const int xx0 = x0 + x * scale;
      const int yy0 = y0 + y * scale;
      for (int sy = 0; sy < scale; ++sy) {
        const int yy = yy0 + sy;
        if (yy < 0 || yy >= LCD_HEIGHT) {
          continue;
        }
        for (int sx = 0; sx < scale; ++sx) {
          const int xx = xx0 + sx;
          if (xx >= 0 && xx < LCD_WIDTH) {
            pm_gfx->writePixel(xx, yy, px);
          }
        }
      }
    }
  }
  image_mux_give();
  return true;
}

void draw_round_tarot_chrome(uint16_t bg, uint16_t ink, uint16_t accent, bool image_drawn) {
  const int r = min(LCD_WIDTH, LCD_HEIGHT) / 2;
  const uint16_t fine = blend565(bg, ink, image_drawn ? 0.38f : 0.18f);
  const uint16_t glow = blend565(bg, accent, image_drawn ? 0.28f : 0.22f);
  pm_gfx->drawCircle(kCx, kCy, r - 5, blend565(bg, accent, image_drawn ? 0.7f : 0.48f));
  pm_gfx->drawCircle(kCx, kCy, r - 9, glow);
  for (int i = 0; i < kCardCount; ++i) {
    const float deg = static_cast<float>(i) * 360.f / static_cast<float>(kCardCount);
    const float ang = pm_face_deg_to_rad(deg);
    const int r0 = r - 22;
    const int r1 = r - ((i % 2) ? 15 : 11);
    pm_gfx->drawLine(kCx + static_cast<int>(lrintf(cosf(ang) * r0)),
                     kCy + static_cast<int>(lrintf(sinf(ang) * r0)),
                     kCx + static_cast<int>(lrintf(cosf(ang) * r1)),
                     kCy + static_cast<int>(lrintf(sinf(ang) * r1)), fine);
  }
}

void draw_shadowed_centered_line(const char *text, int y, uint16_t fg, uint8_t sx, uint8_t sy) {
  if (!text || !text[0]) {
    return;
  }
  const uint16_t shade = pm_gfx->color565(0, 0, 0);
  pm_face_draw_centered_line(text, y + 2, shade, sx, sy);
  pm_face_draw_centered_line(text, y, fg, sx, sy);
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

  (void)load_flash_card_image(idx);
  const bool image_drawn = draw_cached_card_image(idx, kCx, kCy);

  if (!image_drawn) {
    const int R = min(LCD_WIDTH, LCD_HEIGHT) / 2;
    for (int r = R - 6; r > 20; r -= 7) {
      const float a = static_cast<float>(r - 28) / static_cast<float>(R - 34);
      pm_gfx->drawCircle(kCx, kCy, r, blend565(c_bg, c_glow, a * 0.32f));
    }

    pm_gfx->fillCircle(kCx, kCy, 204, blend565(c_bg, c_panel, 0.88f));
    pm_gfx->drawCircle(kCx, kCy, 204, blend565(c_bg, c_accent, 0.62f));
    pm_gfx->drawCircle(kCx, kCy, 188, blend565(c_panel, c_accent, 0.38f));
    draw_card_symbol(idx, kCx, kCy - 2, c_ink, c_accent);
  }

  draw_round_tarot_chrome(c_bg, c_ink, c_accent, image_drawn);

  char num[8];
  snprintf(num, sizeof(num), "%02d", idx);
  draw_shadowed_centered_line(num, 28, c_accent, 2, 2);
  draw_shadowed_centered_line(card.title, 388, c_ink, 2, 2);
  draw_shadowed_centered_line(card.theme, 420, c_dim, 1, 1);

  const char *fallback = s_last_error[0] ? s_last_error : "flash deck";
  const char *deck_hint = s_last_error[0] ? s_last_error : "flash deck";
  if (s_selected < 0) {
    draw_shadowed_centered_line(image_drawn ? "daily major" : fallback, 446, c_dim, 1, 1);
  } else {
    draw_shadowed_centered_line(image_drawn ? "swipe deck  tap daily" : deck_hint, 446, c_dim, 1, 1);
  }
}
