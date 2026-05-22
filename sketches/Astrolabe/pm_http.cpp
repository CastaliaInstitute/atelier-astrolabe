#include "pm_http.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "esp_http_client.h"
#include "esp_err.h"
#include "esp_log.h"
#include "pm_heap.h"

static const char *TAG = "pm_http";
static constexpr int kPmHttpRxBufferBytes = 2048;

extern "C" esp_err_t esp_crt_bundle_attach(void *conf);

static bool url_is_https(const char *url) { return url && strncmp(url, "https://", 8) == 0; }

static esp_http_client_method_t method_from_string(const char *method) {
  if (!method || strcmp(method, "GET") == 0) {
    return HTTP_METHOD_GET;
  }
  if (strcmp(method, "POST") == 0) {
    return HTTP_METHOD_POST;
  }
  return HTTP_METHOD_GET;
}

bool pm_http_request_text(const char *url, const char *method, const char *body,
                          const PmHttpHeader *headers, size_t header_count, char *out,
                          size_t out_cap, int timeout_ms, PmHttpTextResult *result) {
  const uint8_t *body_bytes = reinterpret_cast<const uint8_t *>(body);
  const size_t body_len = body ? strlen(body) : 0;
  return pm_http_request_text_bytes(url, method, body_bytes, body_len, headers, header_count, out,
                                    out_cap, timeout_ms, result, nullptr, 0, nullptr);
}

bool pm_http_request_text_bytes(const char *url, const char *method, const uint8_t *body,
                                size_t body_len, const PmHttpHeader *headers,
                                size_t header_count, char *out, size_t out_cap,
                                int timeout_ms, PmHttpTextResult *result,
                                PmHttpResponseHeader *response_headers,
                                size_t response_header_count, int *content_length) {
  if (result) {
    result->status_code = -1;
    result->bytes_read = 0;
  }
  if (content_length) {
    *content_length = -1;
  }
  if (!url || !out || out_cap < 2) {
    return false;
  }
  out[0] = '\0';

  esp_http_client_config_t config = {};
  config.url = url;
  config.timeout_ms = timeout_ms > 0 ? timeout_ms : 12000;
  config.buffer_size = kPmHttpRxBufferBytes;
  config.buffer_size_tx = 512;
  config.disable_auto_redirect = false;
  config.method = method_from_string(method);
  if (url_is_https(url)) {
    config.crt_bundle_attach = esp_crt_bundle_attach;
  }

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client) {
    return false;
  }
  esp_http_client_set_method(client, method_from_string(method));
  for (size_t i = 0; headers && i < header_count; ++i) {
    if (headers[i].name && headers[i].value) {
      esp_http_client_set_header(client, headers[i].name, headers[i].value);
    }
  }

  bool ok = false;
  if (body_len > static_cast<size_t>(INT_MAX)) {
    esp_http_client_cleanup(client);
    return false;
  }
  esp_err_t err = esp_http_client_open(client, static_cast<int>(body_len));
  pm_heap_trace("network-fetch", -1);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "open failed %s err=%d", url, static_cast<int>(err));
    esp_http_client_cleanup(client);
    return false;
  }
  if (body_len > 0) {
    const int wr = esp_http_client_write(client, reinterpret_cast<const char *>(body), static_cast<int>(body_len));
    if (wr != static_cast<int>(body_len)) {
      ESP_LOGW(TAG, "write failed %s wr=%d len=%u", url, wr, static_cast<unsigned>(body_len));
      esp_http_client_close(client);
      esp_http_client_cleanup(client);
      return false;
    }
  }

  (void)esp_http_client_fetch_headers(client);
  const int status = esp_http_client_get_status_code(client);
  if (result) {
    result->status_code = status;
  }
  if (content_length) {
    *content_length = esp_http_client_get_content_length(client);
  }
  for (size_t i = 0; response_headers && i < response_header_count; ++i) {
    if (!response_headers[i].name || !response_headers[i].value || response_headers[i].value_cap == 0) {
      continue;
    }
    response_headers[i].value[0] = '\0';
    char *value = nullptr;
    if (esp_http_client_get_header(client, response_headers[i].name, &value) == ESP_OK && value) {
      strncpy(response_headers[i].value, value, response_headers[i].value_cap - 1);
      response_headers[i].value[response_headers[i].value_cap - 1] = '\0';
    }
  }
  if (status != 200) {
    ESP_LOGW(TAG, "GET %s -> %d", url, status);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return false;
  }

  size_t rd = 0;
  while (rd + 1 < out_cap) {
    const int n = esp_http_client_read(client, out + rd, static_cast<int>(out_cap - 1 - rd));
    if (n > 0) {
      rd += static_cast<size_t>(n);
      continue;
    }
    if (n == 0) {
      ok = rd > 0;
      break;
    }
    ESP_LOGW(TAG, "read failed %s n=%d", url, n);
    ok = false;
    break;
  }
  out[rd] = '\0';
  if (result) {
    result->bytes_read = rd;
  }
  if (rd + 1 >= out_cap) {
    ESP_LOGW(TAG, "GET %s body too large cap=%u", url, static_cast<unsigned>(out_cap));
    ok = false;
  }

  esp_http_client_close(client);
  esp_http_client_cleanup(client);
  pm_heap_trace(ok ? "network-done" : "network-fail", -1);
  return ok;
}

bool pm_http_request_stream(const char *url, const char *method, const char *body,
                            const PmHttpHeader *headers, size_t header_count, int timeout_ms,
                            PmHttpDataCallback on_data, void *ctx, PmHttpTextResult *result) {
  const uint8_t *body_bytes = reinterpret_cast<const uint8_t *>(body);
  const size_t body_len = body ? strlen(body) : 0;
  return pm_http_request_stream_bytes(url, method, body_bytes, body_len, headers, header_count,
                                      timeout_ms, on_data, ctx, result, nullptr, 0, nullptr);
}

bool pm_http_request_stream_bytes(const char *url, const char *method, const uint8_t *body,
                                  size_t body_len, const PmHttpHeader *headers,
                                  size_t header_count, int timeout_ms,
                                  PmHttpDataCallback on_data, void *ctx,
                                  PmHttpTextResult *result,
                                  PmHttpResponseHeader *response_headers,
                                  size_t response_header_count, int *content_length) {
  if (result) {
    result->status_code = -1;
    result->bytes_read = 0;
  }
  if (content_length) {
    *content_length = -1;
  }
  if (!url || !on_data) {
    return false;
  }

  esp_http_client_config_t config = {};
  config.url = url;
  config.timeout_ms = timeout_ms > 0 ? timeout_ms : 12000;
  config.buffer_size = kPmHttpRxBufferBytes;
  config.buffer_size_tx = 512;
  config.disable_auto_redirect = false;
  config.method = method_from_string(method);
  if (url_is_https(url)) {
    config.crt_bundle_attach = esp_crt_bundle_attach;
  }

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client) {
    return false;
  }
  esp_http_client_set_method(client, method_from_string(method));
  for (size_t i = 0; headers && i < header_count; ++i) {
    if (headers[i].name && headers[i].value) {
      esp_http_client_set_header(client, headers[i].name, headers[i].value);
    }
  }

  if (body_len > static_cast<size_t>(INT_MAX)) {
    esp_http_client_cleanup(client);
    return false;
  }
  esp_err_t err = esp_http_client_open(client, static_cast<int>(body_len));
  pm_heap_trace("network-stream", -1);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "open failed %s err=%d", url, static_cast<int>(err));
    esp_http_client_cleanup(client);
    return false;
  }
  if (body_len > 0) {
    const int wr = esp_http_client_write(client, reinterpret_cast<const char *>(body), static_cast<int>(body_len));
    if (wr != static_cast<int>(body_len)) {
      ESP_LOGW(TAG, "write failed %s wr=%d len=%u", url, wr, static_cast<unsigned>(body_len));
      esp_http_client_close(client);
      esp_http_client_cleanup(client);
      return false;
    }
  }

  (void)esp_http_client_fetch_headers(client);
  const int status = esp_http_client_get_status_code(client);
  if (result) {
    result->status_code = status;
  }
  if (content_length) {
    *content_length = esp_http_client_get_content_length(client);
  }
  for (size_t i = 0; response_headers && i < response_header_count; ++i) {
    if (!response_headers[i].name || !response_headers[i].value || response_headers[i].value_cap == 0) {
      continue;
    }
    response_headers[i].value[0] = '\0';
    char *value = nullptr;
    if (esp_http_client_get_header(client, response_headers[i].name, &value) == ESP_OK && value) {
      strncpy(response_headers[i].value, value, response_headers[i].value_cap - 1);
      response_headers[i].value[response_headers[i].value_cap - 1] = '\0';
    }
  }
  if (status != 200) {
    ESP_LOGW(TAG, "HTTP %s -> %d", url, status);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return false;
  }

  uint8_t *buf = static_cast<uint8_t *>(malloc(kPmHttpRxBufferBytes));
  if (!buf) {
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return false;
  }
  size_t rd = 0;
  bool ok = false;
  for (;;) {
    const int n = esp_http_client_read(client, reinterpret_cast<char *>(buf), kPmHttpRxBufferBytes);
    if (n > 0) {
      if (!on_data(buf, static_cast<size_t>(n), ctx)) {
        ok = false;
        break;
      }
      rd += static_cast<size_t>(n);
      continue;
    }
    if (n == 0) {
      ok = rd > 0;
      break;
    }
    ESP_LOGW(TAG, "read failed %s n=%d", url, n);
    ok = false;
    break;
  }
  if (result) {
    result->bytes_read = rd;
  }
  free(buf);
  esp_http_client_close(client);
  esp_http_client_cleanup(client);
  pm_heap_trace(ok ? "network-stream-done" : "network-stream-fail", -1);
  return ok;
}

bool pm_http_get_text(const char *url, char *out, size_t out_cap, int timeout_ms,
                      PmHttpTextResult *result) {
  return pm_http_request_text(url, "GET", nullptr, nullptr, 0, out, out_cap, timeout_ms, result);
}
