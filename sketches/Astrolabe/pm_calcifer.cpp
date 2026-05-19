#include "pm_calcifer.h"

#include <HTTPClient.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <cstdio>
#include <cstring>

#include "esp_log.h"
#include "pm_castalia_auth.h"
#include "pm_config.h"
#include "pm_heap.h"

static const char *TAG = "pm_calcifer";

static void trim_supabase_url(char *url, size_t cap) {
  if (!url || cap == 0) {
    return;
  }
  while (strlen(url) > 0 && url[strlen(url) - 1] == '/') {
    url[strlen(url) - 1] = '\0';
  }
}

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

static bool extract_json_int64_field(const char *json, const char *key, int64_t *out) {
  char pat[40];
  snprintf(pat, sizeof(pat), "\"%s\":", key);
  const char *p = strstr(json, pat);
  if (!p) {
    return false;
  }
  p += strlen(pat);
  while (*p == ' ') {
    ++p;
  }
  if (*p == 'n') {
    return false;
  }
  char *end = nullptr;
  const long long v = strtoll(p, &end, 10);
  if (end == p) {
    return false;
  }
  *out = static_cast<int64_t>(v);
  return true;
}

static void parse_event_block(const char *json, const char *block_key, PmCalciferEvent *ev) {
  ev->valid = false;
  ev->summary[0] = '\0';
  ev->start_unix = 0;
  ev->end_unix = 0;

  char pat[32];
  snprintf(pat, sizeof(pat), "\"%s\":", block_key);
  const char *p = strstr(json, pat);
  if (!p) {
    return;
  }
  p += strlen(pat);
  while (*p == ' ') {
    ++p;
  }
  if (*p == 'n') {
    return;
  }
  if (*p != '{') {
    return;
  }

  const char *end = strchr(p, '}');
  if (!end) {
    return;
  }
  const size_t n = static_cast<size_t>(end - p + 1);
  char *slice = static_cast<char *>(malloc(n + 1));
  if (!slice) {
    return;
  }
  memcpy(slice, p, n);
  slice[n] = '\0';

  if (extract_json_string_field(slice, "summary", ev->summary, sizeof(ev->summary))) {
    ev->valid = true;
  }
  (void)extract_json_int64_field(slice, "startUnix", &ev->start_unix);
  (void)extract_json_int64_field(slice, "endUnix", &ev->end_unix);
  free(slice);
}

static void parse_status_json(const char *json, PmCalciferStatus *out) {
  out->ok = strstr(json, "\"ok\":true") != nullptr;
  out->configured =
      strstr(json, "\"configured\":true") != nullptr || strstr(json, "\"configured\": true") != nullptr;
  out->error[0] = '\0';
  out->display_tz[0] = '\0';
  out->current = {};
  out->next = {};

  (void)extract_json_string_field(json, "error", out->error, sizeof(out->error));
  (void)extract_json_string_field(json, "displayTz", out->display_tz, sizeof(out->display_tz));
  parse_event_block(json, "current", &out->current);
  parse_event_block(json, "next", &out->next);
}

bool pm_calcifer_fetch(PmCalciferStatus *out, time_t epoch_seconds) {
  if (!out) {
    return false;
  }
  memset(out, 0, sizeof(*out));

  if (strlen(MYNAH_SUPABASE_URL) == 0 || strlen(MYNAH_SUPABASE_ANON_KEY) == 0) {
    snprintf(out->error, sizeof(out->error), "Supabase not configured");
    return false;
  }
  if (!pm_heap_tls_ready(MYNAH_FACE_FETCH_MIN_HEAP, "calcifer")) {
    snprintf(out->error, sizeof(out->error), "low memory");
    return false;
  }

  if (epoch_seconds <= 0) {
    epoch_seconds = time(nullptr);
  }

  char base[160];
  strncpy(base, MYNAH_SUPABASE_URL, sizeof(base) - 1);
  base[sizeof(base) - 1] = '\0';
  trim_supabase_url(base, sizeof(base));

  char url[240];
  snprintf(url, sizeof(url), "%s/functions/v1/calcifer-status", base);

  char body[80];
  snprintf(body, sizeof(body), "{\"epochSeconds\":%lld}", static_cast<long long>(epoch_seconds));

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(25000);
  if (!http.begin(client, url)) {
    snprintf(out->error, sizeof(out->error), "HTTP begin failed");
    return false;
  }
  http.addHeader("Content-Type", "application/json");
  pm_castalia_auth_apply_headers(&http);

  const int code = http.POST(reinterpret_cast<uint8_t *>(body), strlen(body));
  const int streamLen = http.getSize();
  if (code != 200 || streamLen <= 0 || streamLen > 16384) {
    ESP_LOGW(TAG, "calcifer-status HTTP %d len %d", code, streamLen);
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
  const uint32_t deadline = millis() + 20000u;
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

  parse_status_json(resp, out);
  free(resp);
  ESP_LOGI(TAG, "calcifer ok=%d cfg=%d cur=%d next=%d", out->ok ? 1 : 0, out->configured ? 1 : 0,
           out->current.valid ? 1 : 0, out->next.valid ? 1 : 0);
  return out->ok;
}
