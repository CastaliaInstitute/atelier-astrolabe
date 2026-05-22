#include "pm_spotify.h"

#include <cstring>
#include <cstdio>

#include "esp_log.h"
#include "pm_config.h"
#include "pm_castalia_auth.h"
#include "pm_heap.h"
#include "pm_http.h"

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
  if (!pm_castalia_has_session()) {
    snprintf(out->error, sizeof(out->error), "Sign in on Castalia");
    return false;
  }
  if (!pm_castalia_auth_prepare_for_voice()) {
    snprintf(out->error, sizeof(out->error), "Castalia auth failed");
    return false;
  }
  if (!pm_heap_tls_ready(MYNAH_SPOTIFY_MIN_FETCH_HEAP, "spotify")) {
    snprintf(out->error, sizeof(out->error), "low memory");
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

  char bearer[1536];
  char auth[1560];
  pm_castalia_auth_bearer(bearer, sizeof(bearer));
  snprintf(auth, sizeof(auth), "Bearer %s", bearer);
  const PmHttpHeader headers[] = {
      {"Content-Type", "application/json"},
      {"Authorization", auth},
      {"apikey", MYNAH_SUPABASE_ANON_KEY},
  };

  char *resp = static_cast<char *>(pm_heap_alloc_response(8192 + 1));
  if (!resp) {
    snprintf(out->error, sizeof(out->error), "alloc");
    return false;
  }

  PmHttpTextResult result = {};
  if (!pm_http_request_text(url, "POST", body, headers, sizeof(headers) / sizeof(headers[0]), resp,
                            8192 + 1, 20000, &result)) {
    ESP_LOGW(TAG, "mynah-spotify HTTP %d len %u", result.status_code,
             static_cast<unsigned>(result.bytes_read));
    snprintf(out->error, sizeof(out->error), "HTTP %d", result.status_code);
    free(resp);
    return false;
  }

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
