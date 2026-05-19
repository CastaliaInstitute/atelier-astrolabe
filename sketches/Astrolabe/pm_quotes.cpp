#include "pm_quotes.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <cstdio>
#include <cstring>

#include "esp_log.h"
#include "pm_config.h"
#include "pm_heap.h"
#include "pm_wifi_ntp.h"

static const char *TAG = "pm_quotes";
static constexpr const char *kQotdUrl = "https://quotes.castalia.institute/quote-of-the-day.json";
static constexpr size_t kQotdMaxBytes = 8192;

static void copy_json_string(const JsonVariantConst &v, char *out, size_t cap) {
  if (!out || cap == 0) {
    return;
  }
  const char *s = v.is<const char *>() ? v.as<const char *>() : "";
  strncpy(out, s ? s : "", cap - 1);
  out[cap - 1] = '\0';
}

void pm_quotes_fill_demo(PmQuoteOfDay *out) {
  if (!out) {
    return;
  }
  memset(out, 0, sizeof(*out));
  out->ok = true;
  out->demo = true;
  snprintf(out->date, sizeof(out->date), "offline");
  out->index = 1;
  out->total = 1;
  snprintf(out->faculty_slug, sizeof(out->faculty_slug), "a.plato");
  snprintf(out->faculty_name, sizeof(out->faculty_name), "Plato");
  snprintf(out->quote, sizeof(out->quote), "The beginning is the most important part of the work.");
  snprintf(out->passage, sizeof(out->passage), "The Republic, Book II");
  snprintf(out->book_title, sizeof(out->book_title), "The Republic");
  snprintf(out->book_author, sizeof(out->book_author), "Plato");
}

static bool read_http_body(HTTPClient &http, char **out_resp, size_t *out_len) {
  if (!out_resp || !out_len) {
    return false;
  }
  *out_resp = nullptr;
  *out_len = 0;
  const int declared = http.getSize();
  if (declared > 0 && static_cast<size_t>(declared) > kQotdMaxBytes) {
    return false;
  }
  const size_t cap = declared > 0 ? static_cast<size_t>(declared) : kQotdMaxBytes;
  char *resp = static_cast<char *>(pm_heap_alloc_response(cap + 1));
  if (!resp) {
    return false;
  }
  WiFiClient *stream = http.getStreamPtr();
  if (!stream) {
    free(resp);
    return false;
  }
  size_t rd = 0;
  const uint32_t deadline = millis() + 12000u;
  while (rd < cap && static_cast<int32_t>(millis() - deadline) < 0) {
    const int avail = stream->available();
    if (avail > 0) {
      const size_t take = static_cast<size_t>(avail) < (cap - rd) ? static_cast<size_t>(avail) : (cap - rd);
      const int n = stream->readBytes(resp + rd, take);
      if (n > 0) {
        rd += static_cast<size_t>(n);
      }
      if (declared > 0 && rd >= static_cast<size_t>(declared)) {
        break;
      }
    } else if (!http.connected()) {
      break;
    } else {
      delay(2);
    }
  }
  resp[rd] = '\0';
  if (rd == 0) {
    free(resp);
    return false;
  }
  *out_resp = resp;
  *out_len = rd;
  return true;
}

bool pm_quotes_fetch(PmQuoteOfDay *out) {
  if (!out) {
    return false;
  }
  memset(out, 0, sizeof(*out));
  if (!pm_wifi_connected()) {
    snprintf(out->error, sizeof(out->error), "no WiFi");
    pm_quotes_fill_demo(out);
    return out->ok;
  }
  if (!pm_heap_tls_ready(MYNAH_FACE_FETCH_MIN_HEAP, "quotes")) {
    snprintf(out->error, sizeof(out->error), "low memory");
    pm_quotes_fill_demo(out);
    return out->ok;
  }

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(15000);
  if (!http.begin(client, kQotdUrl)) {
    snprintf(out->error, sizeof(out->error), "HTTP begin");
    pm_quotes_fill_demo(out);
    return out->ok;
  }
  http.addHeader("Accept", "application/json");
  const int code = http.GET();
  if (code != 200) {
    ESP_LOGW(TAG, "QOTD HTTP %d", code);
    snprintf(out->error, sizeof(out->error), "HTTP %d", code);
    http.end();
    pm_quotes_fill_demo(out);
    return out->ok;
  }

  char *resp = nullptr;
  size_t len = 0;
  if (!read_http_body(http, &resp, &len)) {
    http.end();
    snprintf(out->error, sizeof(out->error), "empty body");
    pm_quotes_fill_demo(out);
    return out->ok;
  }
  http.end();

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, resp, len);
  free(resp);
  if (err) {
    snprintf(out->error, sizeof(out->error), "parse");
    pm_quotes_fill_demo(out);
    return out->ok;
  }

  JsonObjectConst q = doc["quote"].as<JsonObjectConst>();
  copy_json_string(doc["date"], out->date, sizeof(out->date));
  out->index = doc["index"] | 0;
  out->total = doc["total"] | 0;
  copy_json_string(q["faculty_id"], out->faculty_slug, sizeof(out->faculty_slug));
  copy_json_string(q["faculty_name"], out->faculty_name, sizeof(out->faculty_name));
  copy_json_string(q["quote_text"], out->quote, sizeof(out->quote));
  copy_json_string(q["passage_label"], out->passage, sizeof(out->passage));
  copy_json_string(q["book_title"], out->book_title, sizeof(out->book_title));
  copy_json_string(q["book_author"], out->book_author, sizeof(out->book_author));

  if (out->faculty_slug[0] == '\0' || out->quote[0] == '\0') {
    snprintf(out->error, sizeof(out->error), "missing quote");
    pm_quotes_fill_demo(out);
    return out->ok;
  }
  out->ok = true;
  ESP_LOGI(TAG, "QOTD %s %s", out->date, out->faculty_slug);
  return true;
}
