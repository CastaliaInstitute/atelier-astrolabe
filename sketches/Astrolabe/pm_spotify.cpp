#include "pm_spotify.h"

#include <HTTPClient.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <cstring>
#include <cstdio>
#include <memory>

#include "esp_log.h"
#include "pm_config.h"
#include "pm_castalia_auth.h"
#include "pm_heap.h"

static const char *TAG = "pm_spotify";
static constexpr uint32_t kSpotifyMinInternalFree = 60000u;
static constexpr uint32_t kSpotifyMinLargestInternal = 30000u;

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
  out->album_art_url[0] = '\0';
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
  if (!extract_json_string_field(json, "albumImageUrl", out->album_art_url, sizeof(out->album_art_url))) {
    out->album_art_url[0] = '\0';
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
  const uint32_t free_i = pm_heap_internal_free();
  const uint32_t largest_i = pm_heap_internal_largest();
  if (free_i < kSpotifyMinInternalFree || largest_i < kSpotifyMinLargestInternal) {
    ESP_LOGW(TAG, "spotify low heap: internal=%u largest=%u psram=%u need=%u/%u",
             static_cast<unsigned>(free_i), static_cast<unsigned>(largest_i),
             static_cast<unsigned>(pm_heap_psram_free()), static_cast<unsigned>(kSpotifyMinInternalFree),
             static_cast<unsigned>(kSpotifyMinLargestInternal));
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

  std::unique_ptr<WiFiClientSecure> client(new (std::nothrow) WiFiClientSecure());
  std::unique_ptr<HTTPClient> http(new (std::nothrow) HTTPClient());
  if (!client || !http) {
    snprintf(out->error, sizeof(out->error), "alloc");
    return false;
  }
  client->setInsecure();
  http->setTimeout(20000);
  if (!http->begin(*client, url)) {
    snprintf(out->error, sizeof(out->error), "HTTP begin failed");
    return false;
  }
  http->addHeader("Content-Type", "application/json");
  pm_castalia_auth_apply_headers(http.get());

  const int code = http->POST(reinterpret_cast<uint8_t *>(body), strlen(body));

  const int streamLen = http->getSize();
  if (code != 200 || streamLen > 8192) {
    ESP_LOGW(TAG, "mynah-spotify HTTP %d len %d", code, streamLen);
    snprintf(out->error, sizeof(out->error), "HTTP %d", code);
    http->end();
    return false;
  }

  const size_t resp_cap = streamLen > 0 ? static_cast<size_t>(streamLen) : 8192u;
  char *resp = static_cast<char *>(pm_heap_alloc_response(resp_cap + 1));
  if (!resp) {
    http->end();
    snprintf(out->error, sizeof(out->error), "alloc");
    return false;
  }

  WiFiClient *s = http->getStreamPtr();
  size_t rd = 0;
  const uint32_t read_start = millis();
  while (rd < resp_cap && (s->connected() || s->available()) && millis() - read_start < 5000u) {
    const int avail = s->available();
    if (avail <= 0) {
      delay(10);
      continue;
    }
    const size_t want = min(static_cast<size_t>(avail), resp_cap - rd);
    const int n = s->readBytes(resp + rd, want);
    if (n <= 0) {
      delay(10);
      continue;
    }
    rd += static_cast<size_t>(n);
  }
  resp[rd] = '\0';
  http->end();
  if (rd == 0) {
    free(resp);
    snprintf(out->error, sizeof(out->error), "empty");
    return false;
  }

  parse_status_json(resp, out);
  free(resp);
  return true;
}

struct SpotifyTaskRequest {
  char action[16];
  PmSpotifyStatus status;
  bool result;
  SemaphoreHandle_t done;
};

static void spotify_request_task(void *arg) {
  SpotifyTaskRequest *req = static_cast<SpotifyTaskRequest *>(arg);
  req->result = post_action(req->action, &req->status);
  xSemaphoreGive(req->done);
  vTaskDelete(nullptr);
}

static bool run_action_on_net_task(const char *action, PmSpotifyStatus *out) {
  if (!out) {
    return false;
  }
  SpotifyTaskRequest req = {};
  strncpy(req.action, action ? action : "", sizeof(req.action) - 1);
  req.done = xSemaphoreCreateBinary();
  if (!req.done) {
    memset(out, 0, sizeof(*out));
    snprintf(out->error, sizeof(out->error), "alloc");
    return false;
  }

  TaskHandle_t task = nullptr;
  constexpr uint32_t kSpotifyTaskStack = 24576;
  const BaseType_t started =
      xTaskCreatePinnedToCore(spotify_request_task, "spotify_net", kSpotifyTaskStack, &req, 1, &task, 1);
  if (started != pdPASS) {
    vSemaphoreDelete(req.done);
    memset(out, 0, sizeof(*out));
    snprintf(out->error, sizeof(out->error), "task");
    return false;
  }

  const bool completed = xSemaphoreTake(req.done, pdMS_TO_TICKS(25000)) == pdTRUE;
  vSemaphoreDelete(req.done);
  if (!completed) {
    memset(out, 0, sizeof(*out));
    snprintf(out->error, sizeof(out->error), "timeout");
    return false;
  }
  delay(20);
  memcpy(out, &req.status, sizeof(*out));
  return req.result;
}

bool pm_spotify_refresh(PmSpotifyStatus *out) { return run_action_on_net_task("status", out); }

bool pm_spotify_command(const char *action, PmSpotifyStatus *out) {
  if (!action || !action[0]) {
    return false;
  }
  return run_action_on_net_task(action, out);
}
