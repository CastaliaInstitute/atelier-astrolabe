#include "pm_ephemeris.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "pm_castalia_auth.h"
#include "pm_config.h"

static const char *TAG = "pm_ephemeris";

static void set_err(char *err, size_t err_cap, const char *msg) {
  if (!err || err_cap == 0) {
    return;
  }
  snprintf(err, err_cap, "%s", msg ? msg : "");
}

static void trim_supabase_url(char *url) {
  if (!url) {
    return;
  }
  while (strlen(url) > 0 && url[strlen(url) - 1] == '/') {
    url[strlen(url) - 1] = '\0';
  }
}

static const char *skip_ws(const char *p) {
  while (p && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) {
    ++p;
  }
  return p;
}

static bool parse_json_number_after_colon(const char *p, double *out) {
  if (!p || !out) {
    return false;
  }
  p = strchr(p, ':');
  if (!p) {
    return false;
  }
  p = skip_ws(p + 1);
  char *end = nullptr;
  const double v = strtod(p, &end);
  if (end == p || !isfinite(v)) {
    return false;
  }
  *out = fmod(v, 360.0);
  if (*out < 0.0) {
    *out += 360.0;
  }
  return true;
}

static bool extract_named_lon(const char *json, const char *name, double *lon_out) {
  if (!json || !name || !lon_out) {
    return false;
  }

  char key[24];
  snprintf(key, sizeof(key), "\"%s\"", name);

  char body_pat[40];
  snprintf(body_pat, sizeof(body_pat), "\"body\":\"%s\"", name);
  const char *body_obj = strstr(json, body_pat);
  if (!body_obj) {
    snprintf(body_pat, sizeof(body_pat), "\"body\": \"%s\"", name);
    body_obj = strstr(json, body_pat);
  }
  if (body_obj) {
    const char *near_end = body_obj + 220;
    const char *lon = strstr(body_obj, "\"lon\"");
    if (lon && lon < near_end && parse_json_number_after_colon(lon, lon_out)) {
      return true;
    }
  }

  const char *p = json;
  while ((p = strstr(p, key)) != nullptr) {
    const char *after = skip_ws(p + strlen(key));
    if (*after == ':') {
      after = skip_ws(after + 1);
      if (*after == '{') {
        const char *end_obj = strchr(after, '}');
        const char *lon = strstr(after, "\"lon\"");
        if (lon && (!end_obj || lon < end_obj) && parse_json_number_after_colon(lon, lon_out)) {
          return true;
        }
        lon = strstr(after, "\"longitude\"");
        if (lon && (!end_obj || lon < end_obj) && parse_json_number_after_colon(lon, lon_out)) {
          return true;
        }
      } else if (parse_json_number_after_colon(p, lon_out)) {
        return true;
      }
    }

    const char *near_end = p + 220;
    const char *body_key = strstr(p, "\"body\"");
    const char *lon = strstr(p, "\"lon\"");
    if (body_key && body_key < near_end && lon && lon < near_end &&
        parse_json_number_after_colon(lon, lon_out)) {
      return true;
    }
    p += strlen(key);
  }
  return false;
}

bool pm_ephemeris_fetch(time_t epoch_seconds, PmTransitPositions *out, char *err, size_t err_cap) {
  if (!out) {
    set_err(err, err_cap, "bad out");
    return false;
  }
  if (!MYNAH_ASTROLOGY_REMOTE_EPHEMERIS) {
    set_err(err, err_cap, "disabled");
    return false;
  }
  if (strlen(MYNAH_SUPABASE_URL) == 0 || strlen(MYNAH_SUPABASE_ANON_KEY) == 0) {
    set_err(err, err_cap, "Supabase not configured");
    return false;
  }
  if (epoch_seconds <= 0) {
    epoch_seconds = time(nullptr);
  }
  if (epoch_seconds <= 0) {
    set_err(err, err_cap, "bad epoch");
    return false;
  }

  char base[160];
  strncpy(base, MYNAH_SUPABASE_URL, sizeof(base) - 1);
  base[sizeof(base) - 1] = '\0';
  trim_supabase_url(base);

  char url[240];
  snprintf(url, sizeof(url), "%s/functions/v1/ephemeris", base);

  char body[192];
  snprintf(body, sizeof(body),
           "{\"epochSeconds\":%lld,\"bodies\":[\"sun\",\"moon\",\"mercury\",\"venus\",\"mars\","
           "\"jupiter\",\"saturn\"],\"zodiac\":\"tropical\"}",
           static_cast<long long>(epoch_seconds));

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(18000);
  if (!http.begin(client, url)) {
    set_err(err, err_cap, "HTTP begin failed");
    return false;
  }
  http.addHeader("Content-Type", "application/json");
  pm_castalia_auth_apply_headers(&http);

  const int code = http.POST(reinterpret_cast<uint8_t *>(body), strlen(body));
  const int stream_len = http.getSize();
  if (code != 200 || stream_len <= 0 || stream_len > 12288) {
    ESP_LOGW(TAG, "ephemeris HTTP %d len %d", code, stream_len);
    char tmp[24];
    snprintf(tmp, sizeof(tmp), "HTTP %d", code);
    set_err(err, err_cap, tmp);
    http.end();
    return false;
  }

  char *resp = static_cast<char *>(
      heap_caps_malloc(static_cast<size_t>(stream_len) + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!resp) {
    resp = static_cast<char *>(malloc(static_cast<size_t>(stream_len) + 1));
  }
  if (!resp) {
    http.end();
    set_err(err, err_cap, "alloc");
    return false;
  }

  WiFiClient *stream = http.getStreamPtr();
  size_t rd = 0;
  const uint32_t deadline = millis() + 12000u;
  while (rd < static_cast<size_t>(stream_len)) {
    if (stream->available() > 0) {
      const int n = stream->readBytes(resp + rd, static_cast<size_t>(stream_len) - rd);
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

  static const char *const k_names[kPmBodyCount] = {
      "sun", "moon", "mercury", "venus", "mars", "jupiter", "saturn",
  };

  PmTransitPositions parsed = {};
  for (int i = 0; i < kPmBodyCount; ++i) {
    if (!extract_named_lon(resp, k_names[i], &parsed.lon[i])) {
      free(resp);
      set_err(err, err_cap, "missing body");
      return false;
    }
  }
  free(resp);

  parsed.ok = true;
  *out = parsed;
  set_err(err, err_cap, "");
  ESP_LOGI(TAG, "remote ephemeris ok epoch=%lld", static_cast<long long>(epoch_seconds));
  return true;
}
