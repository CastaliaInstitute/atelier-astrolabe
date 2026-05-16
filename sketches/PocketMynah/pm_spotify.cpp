#include "pm_spotify.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <cstring>
#include <cstdio>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "pm_config.h"

static const char *TAG = "pm_spotify";

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
  return true;
}

static void parse_status_json(const char *json, PmSpotifyStatus *out) {
  out->ok = strstr(json, "\"ok\":true") != nullptr;
  out->is_playing = false;
  out->track[0] = '\0';
  out->artist[0] = '\0';
  out->device[0] = '\0';
  out->error[0] = '\0';
  if (!extract_json_string_field(json, "error", out->error, sizeof(out->error))) {
    out->error[0] = '\0';
  }
  if (!out->ok) {
    return;
  }
  out->is_playing =
      strstr(json, "\"isPlaying\":true") != nullptr || strstr(json, "\"isPlaying\": true") != nullptr;
  if (!extract_json_string_field(json, "track", out->track, sizeof(out->track))) {
    out->track[0] = '\0';
  }
  if (!extract_json_string_field(json, "artist", out->artist, sizeof(out->artist))) {
    out->artist[0] = '\0';
  }
  if (!extract_json_string_field(json, "deviceName", out->device, sizeof(out->device))) {
    out->device[0] = '\0';
  }
}

static bool post_action(const char *action, PmSpotifyStatus *out) {
  if (!out) {
    return false;
  }
  memset(out, 0, sizeof(*out));
  out->ok = false;

  if (strlen(MYNAH_SUPABASE_URL) == 0 || strlen(MYNAH_SUPABASE_ANON_KEY) == 0) {
    snprintf(out->error, sizeof(out->error), "Supabase not configured");
    return false;
  }

  char base[160];
  strncpy(base, MYNAH_SUPABASE_URL, sizeof(base) - 1);
  base[sizeof(base) - 1] = '\0';
  trim_supabase_url(base, sizeof(base));

  char url[224];
  snprintf(url, sizeof(url), "%s/functions/v1/mynah-spotify", base);

  char body[96];
  snprintf(body, sizeof(body), "{\"action\":\"%s\"}", action);

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(20000);
  if (!http.begin(client, url)) {
    snprintf(out->error, sizeof(out->error), "HTTP begin failed");
    return false;
  }
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", String("Bearer ") + MYNAH_SUPABASE_ANON_KEY);
  http.addHeader("apikey", MYNAH_SUPABASE_ANON_KEY);

  const int code = http.POST(reinterpret_cast<uint8_t *>(body), strlen(body));

  const int streamLen = http.getSize();
  if (code != 200 || streamLen <= 0 || streamLen > 8192) {
    ESP_LOGW(TAG, "mynah-spotify HTTP %d len %d", code, streamLen);
    snprintf(out->error, sizeof(out->error), "HTTP %d", code);
    http.end();
    return false;
  }

  char *resp = static_cast<char *>(
      heap_caps_malloc(static_cast<size_t>(streamLen) + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!resp) {
    resp = static_cast<char *>(malloc(static_cast<size_t>(streamLen) + 1));
  }
  if (!resp) {
    http.end();
    snprintf(out->error, sizeof(out->error), "alloc");
    return false;
  }

  WiFiClient *s = http.getStreamPtr();
  size_t rd = 0;
  while (rd < static_cast<size_t>(streamLen) && s->connected()) {
    const int n = s->readBytes(resp + rd, static_cast<size_t>(streamLen) - rd);
    if (n <= 0) {
      break;
    }
    rd += static_cast<size_t>(n);
  }
  resp[rd] = '\0';
  http.end();

  parse_status_json(resp, out);
  free(resp);
  return true;
}

bool pm_spotify_refresh(PmSpotifyStatus *out) { return post_action("status", out); }

bool pm_spotify_command(const char *action, PmSpotifyStatus *out) {
  if (!action || !action[0]) {
    return false;
  }
  return post_action(action, out);
}
