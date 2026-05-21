#include "pm_weather.h"

#include <HTTPClient.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "esp_log.h"
#include "pm_castalia_auth.h"
#include "pm_config.h"
#include "pm_geo_tz.h"
#include "pm_heap.h"
#include "pm_wifi_ntp.h"

static const char *TAG = "pm_weather";

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

static bool extract_json_int_field(const char *json, const char *key, int *out) {
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
  const long v = strtol(p, &end, 10);
  if (end == p) {
    return false;
  }
  *out = static_cast<int>(v);
  return true;
}

static PmWeatherPrecip precip_from_string(const char *s) {
  if (!s || !s[0]) {
    return PmWeatherPrecip::None;
  }
  if (strstr(s, "snow") != nullptr) {
    return PmWeatherPrecip::Snow;
  }
  if (strstr(s, "rain") != nullptr || strstr(s, "wet") != nullptr || strstr(s, "drizzle") != nullptr ||
      strstr(s, "shower") != nullptr) {
    return PmWeatherPrecip::Rain;
  }
  return PmWeatherPrecip::None;
}

static void parse_hourly_array(const char *json, PmWeatherStatus *out) {
  const char *arr = strstr(json, "\"hourly\"");
  if (!arr) {
    return;
  }
  arr = strchr(arr, '[');
  if (!arr) {
    return;
  }
  ++arr;
  for (int i = 0; i < 24; ++i) {
    const char *obj = strchr(arr, '{');
    if (!obj) {
      break;
    }
    const char *end = strchr(obj, '}');
    if (!end) {
      break;
    }
    const size_t n = static_cast<size_t>(end - obj + 1);
    char slice[128];
    if (n >= sizeof(slice)) {
      break;
    }
    memcpy(slice, obj, n);
    slice[n] = '\0';
    int hour = i;
    int temp = 0;
    int hum = 0;
    char precip[16] = {};
    (void)extract_json_int_field(slice, "hour", &hour);
    (void)extract_json_int_field(slice, "tempC", &temp);
    (void)extract_json_int_field(slice, "humidityPct", &hum);
    (void)extract_json_string_field(slice, "precip", precip, sizeof(precip));
    if (hour < 0 || hour > 23) {
      hour = i;
    }
    out->hourly[hour].temp_c = static_cast<int8_t>(temp);
    out->hourly[hour].humidity_pct = static_cast<uint8_t>(hum < 0 ? 0 : (hum > 100 ? 100 : hum));
    out->hourly[hour].precip = precip_from_string(precip);
    arr = end + 1;
  }
}

static void parse_weather_json(const char *json, PmWeatherStatus *out) {
  out->ok = strstr(json, "\"ok\":true") != nullptr;
  out->condition[0] = '\0';
  out->location[0] = '\0';
  int temp = 0;
  int hi = 0;
  int lo = 0;
  (void)extract_json_string_field(json, "condition", out->condition, sizeof(out->condition));
  (void)extract_json_string_field(json, "location", out->location, sizeof(out->location));
  (void)extract_json_int_field(json, "tempC", &temp);
  if (!extract_json_int_field(json, "hiC", &hi)) {
    (void)extract_json_int_field(json, "hi_c", &hi);
  }
  if (!extract_json_int_field(json, "loC", &lo)) {
    (void)extract_json_int_field(json, "lo_c", &lo);
  }
  const char *cur = strstr(json, "\"current\"");
  if (cur) {
    char slice[256];
    const char *obj = strchr(cur, '{');
    const char *end = obj ? strchr(obj, '}') : nullptr;
    if (obj && end && static_cast<size_t>(end - obj + 1) < sizeof(slice)) {
      memcpy(slice, obj, static_cast<size_t>(end - obj + 1));
      slice[end - obj + 1] = '\0';
      (void)extract_json_string_field(slice, "condition", out->condition, sizeof(out->condition));
      (void)extract_json_int_field(slice, "tempC", &temp);
      (void)extract_json_int_field(slice, "hiC", &hi);
      (void)extract_json_int_field(slice, "loC", &lo);
    }
  }
  out->current_temp_c = static_cast<int8_t>(temp);
  out->hi_c = static_cast<int8_t>(hi);
  out->lo_c = static_cast<int8_t>(lo);
  parse_hourly_array(json, out);
}

void pm_weather_fill_demo(PmWeatherStatus *out, int local_hour) {
  if (!out) {
    return;
  }
  memset(out, 0, sizeof(*out));
  out->ok = true;
  out->demo = true;
  snprintf(out->location, sizeof(out->location), "Demo");
  snprintf(out->condition, sizeof(out->condition), "Partly cloudy");

  int8_t cur = 14;
  int8_t hi = 18;
  int8_t lo = 9;
  for (int h = 0; h < 24; ++h) {
    const float phase = static_cast<float>(h) * 0.52f;
    const int8_t t = static_cast<int8_t>(lrintf(11.f + 7.f * sinf(phase) + 2.f * cosf(phase * 0.37f)));
    uint8_t rh = static_cast<uint8_t>(lrintf(55.f + 28.f * sinf(phase * 0.9f + 1.1f)));
    if (rh > 100) {
      rh = 100;
    }
    PmWeatherPrecip p = PmWeatherPrecip::None;
    if (h >= 5 && h <= 8) {
      p = PmWeatherPrecip::Rain;
    } else if (h >= 20 && h <= 22) {
      p = PmWeatherPrecip::Snow;
    }
    out->hourly[h].temp_c = t;
    out->hourly[h].humidity_pct = rh;
    out->hourly[h].precip = p;
    if (h == local_hour) {
      cur = t;
    }
    if (t > hi) {
      hi = t;
    }
    if (t < lo) {
      lo = t;
    }
  }
  out->current_temp_c = cur;
  out->hi_c = hi;
  out->lo_c = lo;
}

bool pm_weather_fetch(PmWeatherStatus *out) {
  if (!out) {
    return false;
  }
  memset(out, 0, sizeof(*out));

  struct tm loc = {};
  int local_hour = 12;
  if (pm_time_valid()) {
    pm_time_local(&loc);
    local_hour = loc.tm_hour;
  }

  if (strlen(MYNAH_SUPABASE_URL) == 0 || strlen(MYNAH_SUPABASE_ANON_KEY) == 0) {
    snprintf(out->error, sizeof(out->error), "Supabase not configured");
    pm_weather_fill_demo(out, local_hour);
    return out->ok;
  }
  if (!pm_castalia_has_session()) {
    snprintf(out->error, sizeof(out->error), "Sign in on Castalia");
    return false;
  }
  if (!pm_castalia_auth_prepare_for_voice()) {
    snprintf(out->error, sizeof(out->error), "Castalia auth failed");
    return false;
  }
  if (!pm_heap_tls_ready(MYNAH_FACE_FETCH_MIN_HEAP, "weather")) {
    snprintf(out->error, sizeof(out->error), "low memory");
    pm_weather_fill_demo(out, local_hour);
    return out->ok;
  }

  char base[160];
  strncpy(base, MYNAH_SUPABASE_URL, sizeof(base) - 1);
  base[sizeof(base) - 1] = '\0';
  trim_supabase_url(base, sizeof(base));

  char url[240];
  snprintf(url, sizeof(url), "%s/functions/v1/weather-status", base);

  const int32_t tz = pm_geo_tz_offset_sec();
  char body[96];
  snprintf(body, sizeof(body), "{\"tzOffsetSec\":%ld}", static_cast<long>(tz));

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(25000);
  if (!http.begin(client, url)) {
    snprintf(out->error, sizeof(out->error), "HTTP begin failed");
    pm_weather_fill_demo(out, local_hour);
    return out->ok;
  }
  http.addHeader("Content-Type", "application/json");
  pm_castalia_auth_apply_headers(&http);

  const int code = http.POST(reinterpret_cast<uint8_t *>(body), strlen(body));
  const int streamLen = http.getSize();
  if (code != 200 || streamLen <= 0 || streamLen > 16384) {
    ESP_LOGW(TAG, "weather-status HTTP %d len %d", code, streamLen);
    snprintf(out->error, sizeof(out->error), "HTTP %d", code);
    http.end();
    pm_weather_fill_demo(out, local_hour);
    return out->ok;
  }

  char *resp = static_cast<char *>(pm_heap_alloc_response(static_cast<size_t>(streamLen) + 1));
  if (!resp) {
    http.end();
    snprintf(out->error, sizeof(out->error), "alloc");
    pm_weather_fill_demo(out, local_hour);
    return out->ok;
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
    pm_weather_fill_demo(out, local_hour);
    return out->ok;
  }

  parse_weather_json(resp, out);
  free(resp);

  if (!out->ok || out->condition[0] == '\0') {
    if (!out->error[0]) {
      snprintf(out->error, sizeof(out->error), "parse");
    }
    pm_weather_fill_demo(out, local_hour);
    return out->ok;
  }

  ESP_LOGI(TAG, "weather ok demo=%d %dC %s", out->demo ? 1 : 0, out->current_temp_c, out->condition);
  return true;
}
