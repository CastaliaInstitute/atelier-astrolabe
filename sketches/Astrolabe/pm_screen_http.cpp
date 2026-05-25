#include "pm_screen_http.h"

#if defined(ASTROLABE_WAVESHARE_S3_185)

void pm_screen_http_begin(PmDisplayCanvas *canvas) { (void)canvas; }

void pm_screen_http_loop() {}

#else

#include <WebServer.h>
#include <WiFi.h>
#include <cstring>

#include "Arduino_GFX_Library.h"
#include "esp32-hal-tinyusb.h"
#include "esp_heap_caps.h"

#include "pm_log.h"
#include "pm_display.h"
#include "pm_wifi_ntp.h"

static WebServer s_server(80);
static PmDisplayCanvas *s_canvas = nullptr;
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
  char html[768];
  snprintf(html, sizeof(html),
           "<!DOCTYPE html><html><head><meta charset=\"utf-8\"><meta name=\"viewport\" "
           "content=\"width=device-width,initial-scale=1\"><title>Astrolabe</title></head>"
           "<body style=\"margin:0;background:#111;color:#ccc;font-family:system-ui,sans-serif;\">"
           "<p style=\"padding:10px;\">Mynah Astrolabe: <a href=\"http://%s/\" "
           "style=\"color:#8cf\">%s</a> <span style=\"color:#778\">mac %s</span> "
           "| <a href=\"/screen.bmp\" style=\"color:#8cf\">screen.bmp</a> "
           "| <a href=\"/logs\" style=\"color:#8cf\">logs</a></p>"
           "<img src=\"/screen.bmp\" style=\"width:100%%;max-width:466px;height:auto;display:block;margin:0 auto;\" "
           "alt=\"screen\"></body></html>",
           pm_wifi_mdns_name(), pm_wifi_mdns_name(), pm_wifi_mac_suffix());
  s_server.send(200, "text/html", html);
}

static void http_send_log_chunk(const char *data, size_t len) {
  if (data && len > 0) {
    s_server.sendContent(data, len);
  }
}

static void handle_logs_txt() {
  if (!pm_wifi_connected()) {
    s_server.send(503, "text/plain", "logs unavailable");
    return;
  }
  s_server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  s_server.send(200, "text/plain", "");
  pm_log_stream_to_http(http_send_log_chunk);
  s_server.sendContent("");
}

static void handle_logs() {
  if (!pm_wifi_connected()) {
    s_server.send(503, "text/plain", "logs unavailable");
    return;
  }
  s_server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  s_server.send(200, "text/html; charset=utf-8", "");
  s_server.sendContent(
      "<!DOCTYPE html><html><head><meta charset=\"utf-8\"><meta name=\"viewport\" "
      "content=\"width=device-width,initial-scale=1\"><meta http-equiv=\"refresh\" content=\"2\">"
      "<title>Astrolabe Logs</title></head><body style=\"margin:0;background:#0b0c10;color:#d7dde8;"
      "font:13px ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;\">"
      "<div style=\"position:sticky;top:0;background:#151821;padding:8px 10px;\">"
      "<a href=\"/\" style=\"color:#8cf\">screen</a> <a href=\"/logs.txt\" style=\"color:#8cf\">logs.txt</a>"
      "</div><pre style=\"white-space:pre-wrap;margin:0;padding:10px;\">");
  pm_log_stream_to_http(http_send_log_chunk);
  s_server.sendContent("</pre></body></html>");
  s_server.sendContent("");
}

static void handle_bootloader() {
  pm_log_printf(false, "http: entering USB CDC bootloader");
  s_server.send(200, "text/plain", "entering bootloader\n");
  delay(100);
  usb_persist_restart(RESTART_BOOTLOADER);
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

void pm_screen_http_begin(PmDisplayCanvas *canvas) {
  s_canvas = canvas;
  if (s_http_started || !canvas || !pm_wifi_connected()) {
    return;
  }
  s_server.on("/", HTTP_GET, handle_root);
  s_server.on("/screen.bmp", HTTP_GET, handle_screen_bmp);
  s_server.on("/logs", HTTP_GET, handle_logs);
  s_server.on("/logs.txt", HTTP_GET, handle_logs_txt);
  s_server.on("/bootloader", HTTP_POST, handle_bootloader);
  s_server.begin();
  s_http_started = true;
  pm_log_printf(false, "http: ready http://%s/ ip=%s", pm_wifi_mdns_name(),
                WiFi.localIP().toString().c_str());
  Serial.printf("Screen over WiFi: http://%s/ or http://%s/  (GET /screen.bmp)\n",
                pm_wifi_mdns_name(), WiFi.localIP().toString().c_str());
}

void pm_screen_http_loop() {
  if (!s_http_started) {
    if (s_canvas && pm_wifi_connected()) {
      pm_screen_http_begin(s_canvas);
    }
    return;
  }
  if (!pm_wifi_connected()) {
    return;
  }
  s_server.handleClient();
}

#endif
