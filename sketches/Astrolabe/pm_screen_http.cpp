#include "pm_screen_http.h"

#include <cstring>

#include "Arduino_GFX_Library.h"
#include "esp32-hal-tinyusb.h"
#include "esp_http_server.h"
#include "esp_heap_caps.h"

#include "pm_log.h"
#include "pm_wifi_ntp.h"

static httpd_handle_t s_server = nullptr;
static httpd_req_t *s_chunk_req = nullptr;
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

static esp_err_t handle_root(httpd_req_t *req) {
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
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
}

static void http_send_log_chunk(const char *data, size_t len) {
  if (s_chunk_req && data && len > 0) {
    (void)httpd_resp_send_chunk(s_chunk_req, data, len);
  }
}

static esp_err_t handle_logs_txt(httpd_req_t *req) {
  if (!pm_wifi_connected()) {
    httpd_resp_set_status(req, "503 Service Unavailable");
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_sendstr(req, "logs unavailable");
  }
  httpd_resp_set_type(req, "text/plain");
  s_chunk_req = req;
  pm_log_stream_to_http(http_send_log_chunk);
  s_chunk_req = nullptr;
  return httpd_resp_send_chunk(req, nullptr, 0);
}

static esp_err_t handle_logs(httpd_req_t *req) {
  if (!pm_wifi_connected()) {
    httpd_resp_set_status(req, "503 Service Unavailable");
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_sendstr(req, "logs unavailable");
  }
  httpd_resp_set_type(req, "text/html; charset=utf-8");
  httpd_resp_send_chunk(
      req,
      "<!DOCTYPE html><html><head><meta charset=\"utf-8\"><meta name=\"viewport\" "
      "content=\"width=device-width,initial-scale=1\"><meta http-equiv=\"refresh\" content=\"2\">"
      "<title>Astrolabe Logs</title></head><body style=\"margin:0;background:#0b0c10;color:#d7dde8;"
      "font:13px ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;\">"
      "<div style=\"position:sticky;top:0;background:#151821;padding:8px 10px;\">"
      "<a href=\"/\" style=\"color:#8cf\">screen</a> <a href=\"/logs.txt\" style=\"color:#8cf\">logs.txt</a>"
      "</div><pre style=\"white-space:pre-wrap;margin:0;padding:10px;\">",
      HTTPD_RESP_USE_STRLEN);
  s_chunk_req = req;
  pm_log_stream_to_http(http_send_log_chunk);
  s_chunk_req = nullptr;
  httpd_resp_send_chunk(req, "</pre></body></html>", HTTPD_RESP_USE_STRLEN);
  return httpd_resp_send_chunk(req, nullptr, 0);
}

static esp_err_t handle_bootloader(httpd_req_t *req) {
  pm_log_printf(false, "http: entering USB CDC bootloader");
  httpd_resp_set_type(req, "text/plain");
  httpd_resp_sendstr(req, "entering bootloader\n");
  delay(100);
  usb_persist_restart(RESTART_BOOTLOADER);
  return ESP_OK;
}

static esp_err_t handle_screen_bmp(httpd_req_t *req) {
  if (!s_canvas || !pm_wifi_connected()) {
    httpd_resp_set_status(req, "503 Service Unavailable");
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_sendstr(req, "screen unavailable");
  }
  uint16_t *fb = s_canvas->getFramebuffer();
  if (!fb) {
    httpd_resp_set_status(req, "503 Service Unavailable");
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_sendstr(req, "no framebuffer");
  }

  const int32_t w = s_canvas->width();
  const int32_t h = s_canvas->height();
  if (w <= 0 || h <= 0 || w > 1024 || h > 1024) {
    httpd_resp_set_status(req, "500 Internal Server Error");
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_sendstr(req, "bad size");
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
    httpd_resp_set_status(req, "500 Internal Server Error");
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_sendstr(req, "alloc failed");
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

  httpd_resp_set_type(req, "image/bmp");
  const esp_err_t rc = httpd_resp_send(req, reinterpret_cast<const char *>(buf), file_size);
  free(buf);
  return rc;
}

static void register_get(const char *uri, esp_err_t (*handler)(httpd_req_t *)) {
  httpd_uri_t h = {};
  h.uri = uri;
  h.method = HTTP_GET;
  h.handler = handler;
  h.user_ctx = nullptr;
  (void)httpd_register_uri_handler(s_server, &h);
}

static void register_post(const char *uri, esp_err_t (*handler)(httpd_req_t *)) {
  httpd_uri_t h = {};
  h.uri = uri;
  h.method = HTTP_POST;
  h.handler = handler;
  h.user_ctx = nullptr;
  (void)httpd_register_uri_handler(s_server, &h);
}

void pm_screen_http_begin(Arduino_Canvas *canvas) {
  s_canvas = canvas;
  if (s_http_started || !canvas || !pm_wifi_connected()) {
    return;
  }
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;
  config.max_uri_handlers = 8;
  config.stack_size = 6144;
  config.lru_purge_enable = true;
  if (httpd_start(&s_server, &config) != ESP_OK || !s_server) {
    s_server = nullptr;
    pm_log_printf(false, "http: esp_http_server start failed");
    return;
  }
  register_get("/", handle_root);
  register_get("/screen.bmp", handle_screen_bmp);
  register_get("/logs", handle_logs);
  register_get("/logs.txt", handle_logs_txt);
  register_post("/bootloader", handle_bootloader);
  s_http_started = true;
  pm_log_printf(false, "http: ready http://%s/ ip=%s", pm_wifi_mdns_name(),
                pm_wifi_local_ip());
  Serial.printf("Screen over WiFi: http://%s/ or http://%s/  (GET /screen.bmp)\n",
                pm_wifi_mdns_name(), pm_wifi_local_ip());
}

void pm_screen_http_loop() {
  if (!s_http_started) {
    if (s_canvas && pm_wifi_connected()) {
      pm_screen_http_begin(s_canvas);
    }
    return;
  }
}
