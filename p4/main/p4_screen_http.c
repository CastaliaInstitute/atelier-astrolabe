#include "p4_screen_http.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_http_server.h"
#include "esp_log.h"
#include "p4_network.h"
#include "p4_real_ui.h"

static const char *TAG = "astrolabe_http";
static httpd_handle_t s_httpd;

static void put_le16(uint8_t *p, uint16_t v) {
  p[0] = (uint8_t)(v & 0xffu);
  p[1] = (uint8_t)((v >> 8) & 0xffu);
}

static void put_le32(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)(v & 0xffu);
  p[1] = (uint8_t)((v >> 8) & 0xffu);
  p[2] = (uint8_t)((v >> 16) & 0xffu);
  p[3] = (uint8_t)((v >> 24) & 0xffu);
}

static esp_err_t root_get_handler(httpd_req_t *req) {
  astrolabe_p4_network_status_t net = astrolabe_p4_network_status();
  char html[640];
  snprintf(html, sizeof(html),
           "<!doctype html><html><head><meta charset=\"utf-8\"><meta name=\"viewport\" "
           "content=\"width=device-width,initial-scale=1\"><title>Astrolabe P4</title></head>"
           "<body style=\"margin:0;background:#05070a;color:#dbe3ef;font-family:system-ui,sans-serif\">"
           "<p style=\"padding:10px;margin:0\">Astrolabe P4 %s | <a style=\"color:#8cf\" "
           "href=\"/screen.bmp\">screen.bmp</a></p>"
           "<img src=\"/screen.bmp\" style=\"width:100%%;max-width:466px;display:block;margin:0 auto\"></body></html>",
           net.ip);
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t screen_bmp_get_handler(httpd_req_t *req) {
  const uint16_t *fb = astrolabe_real_ui_framebuffer();
  const int32_t src_w = astrolabe_real_ui_width();
  const int32_t src_h = astrolabe_real_ui_height();
  const int32_t scale = 2;
  const int32_t w = src_w / scale;
  const int32_t h = src_h / scale;
  if (fb == NULL || src_w <= 0 || src_h <= 0 || w <= 0 || h <= 0 || src_w > 1024 || src_h > 1024) {
    httpd_resp_set_status(req, "503 Service Unavailable");
    httpd_resp_send(req, "screen unavailable", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  }

  const uint32_t row_stride = (((uint32_t)w * 24u + 31u) / 32u) * 4u;
  const uint32_t pixel_bytes = row_stride * (uint32_t)h;
  const uint32_t file_size = 54u + pixel_bytes;
  uint8_t header[54] = {};
  header[0] = 'B';
  header[1] = 'M';
  put_le32(header + 2, file_size);
  put_le32(header + 10, 54u);
  put_le32(header + 14, 40u);
  put_le32(header + 18, (uint32_t)w);
  put_le32(header + 22, (uint32_t)h);
  put_le16(header + 26, 1u);
  put_le16(header + 28, 24u);
  put_le32(header + 34, pixel_bytes);

  uint8_t *row = (uint8_t *)calloc(1, row_stride);
  if (row == NULL) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "row alloc failed");
    return ESP_OK;
  }

  char len[16];
  snprintf(len, sizeof(len), "%lu", (unsigned long)file_size);
  httpd_resp_set_type(req, "image/bmp");
  httpd_resp_set_hdr(req, "Content-Length", len);
  esp_err_t ret = httpd_resp_send_chunk(req, (const char *)header, sizeof(header));
  if (ret != ESP_OK) {
    free(row);
    return ret;
  }

  for (int32_t yi = 0; yi < h; ++yi) {
    const int32_t y = h - 1 - yi;
    uint8_t *dst = row;
    const uint16_t *src = fb + (y * scale) * src_w;
    memset(row, 0, row_stride);
    for (int32_t x = 0; x < w; ++x) {
      const uint16_t c = src[x * scale];
      const unsigned r5 = (c >> 11) & 0x1fu;
      const unsigned g6 = (c >> 5) & 0x3fu;
      const unsigned b5 = c & 0x1fu;
      *dst++ = (uint8_t)((b5 * 255u + 15u) / 31u);
      *dst++ = (uint8_t)((g6 * 255u + 31u) / 63u);
      *dst++ = (uint8_t)((r5 * 255u + 15u) / 31u);
    }
    ret = httpd_resp_send_chunk(req, (const char *)row, row_stride);
    if (ret != ESP_OK) {
      free(row);
      return ret;
    }
  }

  free(row);
  return httpd_resp_send_chunk(req, NULL, 0);
}

esp_err_t astrolabe_p4_screen_http_start(void) {
  if (s_httpd != NULL) {
    return ESP_OK;
  }
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.lru_purge_enable = true;
  esp_err_t ret = httpd_start(&s_httpd, &config);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "screen HTTP start failed: %s", esp_err_to_name(ret));
    return ret;
  }

  const httpd_uri_t root = {
      .uri = "/",
      .method = HTTP_GET,
      .handler = root_get_handler,
      .user_ctx = NULL,
  };
  const httpd_uri_t screen = {
      .uri = "/screen.bmp",
      .method = HTTP_GET,
      .handler = screen_bmp_get_handler,
      .user_ctx = NULL,
  };
  ESP_ERROR_CHECK_WITHOUT_ABORT(httpd_register_uri_handler(s_httpd, &root));
  ESP_ERROR_CHECK_WITHOUT_ABORT(httpd_register_uri_handler(s_httpd, &screen));
  astrolabe_p4_network_status_t net = astrolabe_p4_network_status();
  ESP_LOGI(TAG, "screen HTTP ready: http://%s/screen.bmp", net.ip);
  return ESP_OK;
}
