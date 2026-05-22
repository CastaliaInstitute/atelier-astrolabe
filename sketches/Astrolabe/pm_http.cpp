#include "pm_http.h"

#include <string.h>

#include "esp_http_client.h"
#include "esp_log.h"

static const char *TAG = "pm_http";

bool pm_http_get_text(const char *url, char *out, size_t out_cap, int timeout_ms,
                      PmHttpTextResult *result) {
  if (result) {
    result->status_code = -1;
    result->bytes_read = 0;
  }
  if (!url || !out || out_cap < 2) {
    return false;
  }
  out[0] = '\0';

  esp_http_client_config_t config = {};
  config.url = url;
  config.timeout_ms = timeout_ms > 0 ? timeout_ms : 12000;
  config.buffer_size = 512;
  config.buffer_size_tx = 512;
  config.disable_auto_redirect = false;

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client) {
    return false;
  }
  esp_http_client_set_method(client, HTTP_METHOD_GET);

  bool ok = false;
  esp_err_t err = esp_http_client_open(client, 0);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "open failed %s err=%d", url, static_cast<int>(err));
    esp_http_client_cleanup(client);
    return false;
  }

  (void)esp_http_client_fetch_headers(client);
  const int status = esp_http_client_get_status_code(client);
  if (result) {
    result->status_code = status;
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
  return ok;
}
