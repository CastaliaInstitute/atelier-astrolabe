#include "pm_calcifer.h"

#include <cstdio>
#include <cstring>

#include "esp_log.h"
#include "pm_castalia_auth.h"
#include "pm_config.h"
#include "pm_heap.h"
#include "pm_http.h"

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
  if (!pm_castalia_has_session()) {
    snprintf(out->error, sizeof(out->error), "Sign in on Castalia");
    return false;
  }
  if (!pm_castalia_auth_prepare_for_voice()) {
    snprintf(out->error, sizeof(out->error), "Castalia auth failed");
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

  char bearer[1536];
  char auth[1560];
  pm_castalia_auth_bearer(bearer, sizeof(bearer));
  snprintf(auth, sizeof(auth), "Bearer %s", bearer);
  const PmHttpHeader headers[] = {
      {"Content-Type", "application/json"},
      {"Authorization", auth},
      {"apikey", MYNAH_SUPABASE_ANON_KEY},
  };

  char *resp = static_cast<char *>(pm_heap_alloc_response(16384 + 1));
  if (!resp) {
    snprintf(out->error, sizeof(out->error), "alloc");
    return false;
  }

  PmHttpTextResult result = {};
  if (!pm_http_request_text(url, "POST", body, headers, sizeof(headers) / sizeof(headers[0]), resp,
                            16384 + 1, 25000, &result)) {
    ESP_LOGW(TAG, "calcifer-status HTTP %d len %u", result.status_code,
             static_cast<unsigned>(result.bytes_read));
    free(resp);
    snprintf(out->error, sizeof(out->error), "HTTP %d", result.status_code);
    return false;
  }

  parse_status_json(resp, out);
  free(resp);
  ESP_LOGI(TAG, "calcifer ok=%d cfg=%d cur=%d next=%d", out->ok ? 1 : 0, out->configured ? 1 : 0,
           out->current.valid ? 1 : 0, out->next.valid ? 1 : 0);
  return out->ok;
}
