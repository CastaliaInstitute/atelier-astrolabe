#include "pm_hafez.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "pm_castalia_auth.h"
#include "pm_config.h"

static const char *TAG = "pm_hafez";
static constexpr size_t kRespMaxBytes = 12288;

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

static bool extract_json_bool_field(const char *json, const char *key, bool *out) {
  char pat[32];
  snprintf(pat, sizeof(pat), "\"%s\":", key);
  const char *p = strstr(json, pat);
  if (!p) {
    return false;
  }
  p += strlen(pat);
  while (*p == ' ') {
    ++p;
  }
  if (strncmp(p, "true", 4) == 0) {
    *out = true;
    return true;
  }
  if (strncmp(p, "false", 5) == 0) {
    *out = false;
    return true;
  }
  return false;
}

static bool extract_json_uint32_field(const char *json, const char *key, uint32_t *out) {
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
  char *end = nullptr;
  const unsigned long v = strtoul(p, &end, 10);
  if (end == p) {
    return false;
  }
  *out = static_cast<uint32_t>(v);
  return true;
}

static bool read_small_json_body(HTTPClient *http, char **out_resp) {
  WiFiClient *stream = http->getStreamPtr();
  if (!stream) {
    return false;
  }
  char *buf = static_cast<char *>(heap_caps_malloc(kRespMaxBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!buf) {
    buf = static_cast<char *>(malloc(kRespMaxBytes));
  }
  if (!buf) {
    return false;
  }
  size_t rd = 0;
  const uint32_t deadline = millis() + 60000u;
  while (rd < kRespMaxBytes - 1 && static_cast<int32_t>(millis() - deadline) < 0) {
    const int avail = stream->available();
    if (avail > 0) {
      const size_t take = static_cast<size_t>(avail) < (kRespMaxBytes - 1 - rd)
                              ? static_cast<size_t>(avail)
                              : (kRespMaxBytes - 1 - rd);
      const int n = stream->readBytes(buf + rd, take);
      if (n > 0) {
        rd += static_cast<size_t>(n);
      }
    } else if (!http->connected()) {
      break;
    } else {
      delay(4);
    }
  }
  buf[rd] = '\0';
  if (rd == 0) {
    free(buf);
    return false;
  }
  *out_resp = buf;
  return true;
}

static void parse_status_json(const char *json, PmHafezStatus *out) {
  out->ok = strstr(json, "\"ok\":true") != nullptr || strstr(json, "\"ok\": true") != nullptr;
  out->configured =
      strstr(json, "\"configured\":true") != nullptr || strstr(json, "\"configured\": true") != nullptr;
  out->quote[0] = '\0';
  out->source[0] = '\0';
  out->art_prompt[0] = '\0';
  out->palette[0] = '\0';
  out->art_seed = 0;
  out->day_key = 0;
  out->error[0] = '\0';

  (void)extract_json_string_field(json, "quote", out->quote, sizeof(out->quote));
  (void)extract_json_string_field(json, "source", out->source, sizeof(out->source));
  (void)extract_json_string_field(json, "artPrompt", out->art_prompt, sizeof(out->art_prompt));
  (void)extract_json_string_field(json, "palette", out->palette, sizeof(out->palette));
  (void)extract_json_string_field(json, "error", out->error, sizeof(out->error));
  (void)extract_json_uint32_field(json, "artSeed", &out->art_seed);
  (void)extract_json_uint32_field(json, "dayKey", &out->day_key);

  bool configured = false;
  if (extract_json_bool_field(json, "configured", &configured)) {
    out->configured = configured;
  }
}

bool pm_hafez_fetch(PmHafezStatus *out, time_t epoch_seconds) {
  if (!out) {
    return false;
  }
  memset(out, 0, sizeof(*out));

  if (strlen(MYNAH_SUPABASE_URL) == 0 || strlen(MYNAH_SUPABASE_ANON_KEY) == 0) {
    snprintf(out->error, sizeof(out->error), "Supabase not configured");
    return false;
  }

  if (epoch_seconds <= 0) {
    epoch_seconds = time(nullptr);
  }

  char base[160];
  strncpy(base, MYNAH_SUPABASE_URL, sizeof(base) - 1);
  base[sizeof(base) - 1] = '\0';
  trim_supabase_url(base, sizeof(base));

  char url[224];
  snprintf(url, sizeof(url), "%s/functions/v1/hafez-daily", base);

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
  if (code != 200) {
    ESP_LOGW(TAG, "hafez-daily HTTP %d", code);
    if (code == 401) {
      snprintf(out->error, sizeof(out->error), "sign in on Castalia face");
    } else {
      snprintf(out->error, sizeof(out->error), "HTTP %d", code);
    }
    http.end();
    return false;
  }

  char *resp = nullptr;
  if (!read_small_json_body(&http, &resp)) {
    http.end();
    snprintf(out->error, sizeof(out->error), "empty response");
    return false;
  }
  http.end();

  parse_status_json(resp, out);
  free(resp);

  if (!out->ok && out->error[0] == '\0') {
    snprintf(out->error, sizeof(out->error), "unavailable");
  }
  if (out->source[0] == '\0') {
    snprintf(out->source, sizeof(out->source), "Hafez");
  }
  ESP_LOGI(TAG, "hafez ok=%d cfg=%d day=%u", out->ok ? 1 : 0, out->configured ? 1 : 0,
           static_cast<unsigned>(out->day_key));
  return out->ok;
}
