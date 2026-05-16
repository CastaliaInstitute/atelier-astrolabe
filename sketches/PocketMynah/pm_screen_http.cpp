#include "pm_screen_http.h"

#include <WebServer.h>
#include <WiFi.h>
#include <cstring>

#include "Arduino_GFX_Library.h"
#include "esp_heap_caps.h"

#include "pm_wifi_ntp.h"

static WebServer s_server(80);
static Arduino_Canvas *s_canvas = nullptr;
static bool s_http_started = false;

static void put_le32(uint8_t *p, uint32_t v) {
  p[0] = static_cast<uint8_t>(v & 0xffu);
  p[1] = static_cast<uint8_t>((v >> 8) & 0xffu);
  p[2] = static_cast<uint8_t>((v >> 16) & 0xffu);
  p[3] = static_cast<uint8_t>((v >> 24) & 0xffu);
}

static void put_le16(uint8_t *p, uint16_t v) {
  p[0] = static_cast<uint8_t>(v & 0xffu);
  p[1] = static_cast<uint8_t>((v >> 8) & 0xffu);
}

static void handle_root() {
  static const char kHtml[] PROGMEM =
      "<!DOCTYPE html><html><head><meta charset=\"utf-8\"><meta name=\"viewport\" "
      "content=\"width=device-width,initial-scale=1\"><title>PocketMynah</title></head>"
      "<body style=\"margin:0;background:#111;color:#ccc;font-family:system-ui,sans-serif;\">"
      "<p style=\"padding:10px;\">Frame grab: <a href=\"/screen.bmp\" style=\"color:#8cf\">screen.bmp</a></p>"
      "<img src=\"/screen.bmp\" style=\"width:100%;max-width:466px;height:auto;display:block;margin:0 auto;\" "
      "alt=\"screen\"></body></html>";
  s_server.send_P(200, "text/html", kHtml);
}

static void handle_screen_bmp() {
  if (!s_canvas || !pm_wifi_connected()) {
    s_server.send(503, "text/plain", "screen unavailable");
    return;
  }
  uint16_t *fb = s_canvas->getFramebuffer();
  if (!fb) {
    s_server.send(503, "text/plain", "no framebuffer");
    return;
  }

  const int32_t w = s_canvas->width();
  const int32_t h = s_canvas->height();
  if (w <= 0 || h <= 0 || w > 1024 || h > 1024) {
    s_server.send(500, "text/plain", "bad size");
    return;
  }

  const uint32_t row_stride = ((static_cast<uint32_t>(w) * 24u + 31u) / 32u) * 4u;
  const uint32_t pixel_bytes = row_stride * static_cast<uint32_t>(h);
  const uint32_t file_size = 54u + pixel_bytes;

  uint8_t *buf = static_cast<uint8_t *>(
      heap_caps_malloc(file_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!buf) {
    buf = static_cast<uint8_t *>(malloc(file_size));
  }
  if (!buf) {
    s_server.send(500, "text/plain", "alloc failed");
    return;
  }

  std::memset(buf, 0, file_size);
  buf[0] = 'B';
  buf[1] = 'M';
  put_le32(buf + 2, file_size);
  put_le32(buf + 10, 54u);
  put_le32(buf + 14, 40u);
  put_le32(buf + 18, static_cast<uint32_t>(w));
  put_le32(buf + 22, static_cast<uint32_t>(h));
  put_le16(buf + 26, 1u);
  put_le16(buf + 28, 24u);
  put_le32(buf + 34, pixel_bytes);

  uint16_t *snap = static_cast<uint16_t *>(
      heap_caps_malloc(static_cast<size_t>(w) * static_cast<size_t>(h) * 2u,
                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!snap) {
    snap = static_cast<uint16_t *>(malloc(static_cast<size_t>(w) * static_cast<size_t>(h) * 2u));
  }
  if (snap) {
    std::memcpy(snap, fb, static_cast<size_t>(w) * static_cast<size_t>(h) * 2u);
  } else {
    snap = fb;
  }

  uint8_t *pix = buf + 54;
  for (int32_t yi = 0; yi < h; ++yi) {
    const int32_t y = h - 1 - yi;
    uint8_t *dst = pix + static_cast<uint32_t>(yi) * row_stride;
    const uint16_t *src = snap + static_cast<int32_t>(y) * w;
    for (int32_t x = 0; x < w; ++x) {
      const uint16_t c = src[x];
      const unsigned r5 = (c >> 11) & 0x1fu;
      const unsigned g6 = (c >> 5) & 0x3fu;
      const unsigned b5 = c & 0x1fu;
      *dst++ = static_cast<uint8_t>((b5 * 255u + 15u) / 31u);
      *dst++ = static_cast<uint8_t>((g6 * 255u + 31u) / 63u);
      *dst++ = static_cast<uint8_t>((r5 * 255u + 15u) / 31u);
    }
    for (uint32_t pad = static_cast<uint32_t>(w) * 3u; pad < row_stride; ++pad) {
      *dst++ = 0;
    }
  }

  if (snap != fb) {
    free(snap);
  }

  s_server.setContentLength(file_size);
  s_server.send(200, "image/bmp", "");
  s_server.sendContent(reinterpret_cast<const char *>(buf), file_size);
  free(buf);
}

void pm_screen_http_begin(Arduino_Canvas *canvas) {
  s_canvas = canvas;
  if (s_http_started || !canvas || !pm_wifi_connected()) {
    return;
  }
  s_server.on("/", HTTP_GET, handle_root);
  s_server.on("/screen.bmp", HTTP_GET, handle_screen_bmp);
  s_server.begin();
  s_http_started = true;
  Serial.printf("Screen over WiFi: http://%s/  (GET /screen.bmp)\n", WiFi.localIP().toString().c_str());
}

void pm_screen_http_loop() {
  if (!s_http_started) {
    return;
  }
  if (!pm_wifi_connected()) {
    return;
  }
  s_server.handleClient();
}
