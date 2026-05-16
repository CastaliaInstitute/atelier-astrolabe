#include "pm_voice.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <mbedtls/base64.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "pm_config.h"

static const char *TAG = "pm_voice";

void pm_voice_result_free(PmVoiceResult *r) {
  if (!r) {
    return;
  }
  free(r->mp3);
  r->mp3 = nullptr;
  r->mp3_len = 0;
}

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

static bool extract_audio_base64(const char *json, uint8_t **out_bin, size_t *out_len) {
  const char *k = "\"audioBase64\":\"";
  const char *p = strstr(json, k);
  if (!p) {
    return false;
  }
  p += strlen(k);
  const char *start = p;
  while (*p && *p != '"') {
    ++p;
  }
  const size_t b64len = static_cast<size_t>(p - start);
  if (b64len == 0) {
    return false;
  }
  size_t olen = 0;
  const size_t guess = (b64len * 3) / 4 + 8;
  uint8_t *buf = static_cast<uint8_t *>(
      heap_caps_malloc(guess, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!buf) {
    buf = static_cast<uint8_t *>(malloc(guess));
  }
  if (!buf) {
    return false;
  }
  if (mbedtls_base64_decode(buf, guess, &olen, reinterpret_cast<const unsigned char *>(start), b64len) != 0) {
    free(buf);
    return false;
  }
  *out_bin = buf;
  *out_len = olen;
  return true;
}

bool pm_voice_post_pcm(const uint8_t *pcm, size_t pcm_len, PmVoiceResult *r) {
  if (!r || !pcm || pcm_len == 0) {
    return false;
  }
  memset(r->transcript, 0, sizeof(r->transcript));
  memset(r->reply, 0, sizeof(r->reply));
  r->mp3 = nullptr;
  r->mp3_len = 0;

  if (strlen(MYNAH_SUPABASE_URL) == 0 || strlen(MYNAH_SUPABASE_ANON_KEY) == 0) {
    ESP_LOGW(TAG, "Supabase URL or anon key empty");
    return false;
  }

  char base[160];
  strncpy(base, MYNAH_SUPABASE_URL, sizeof(base) - 1);
  base[sizeof(base) - 1] = '\0';
  trim_supabase_url(base, sizeof(base));

  char url[224];
  snprintf(url, sizeof(url), "%s/functions/v1/voice-pipeline", base);

  static const char kPrefix[] =
      "{\"languageCode\":\"en-US\",\"sampleRateHertz\":16000,\"audioBase64\":\"";
  static const char kSuffix[] = "\"}";
  const size_t b64max = ((pcm_len + 2) / 3) * 4 + 4;
  const size_t body_cap = sizeof(kPrefix) - 1 + b64max + sizeof(kSuffix) - 1;
  uint8_t *body = static_cast<uint8_t *>(
      heap_caps_malloc(body_cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!body) {
    body = static_cast<uint8_t *>(malloc(body_cap));
  }
  if (!body) {
    return false;
  }
  memcpy(body, kPrefix, sizeof(kPrefix) - 1);
  size_t nout = 0;
  if (mbedtls_base64_encode(
          body + sizeof(kPrefix) - 1,
          body_cap - (sizeof(kPrefix) - 1),
          &nout,
          pcm,
          pcm_len) != 0) {
    free(body);
    return false;
  }
  memcpy(body + sizeof(kPrefix) - 1 + nout, kSuffix, sizeof(kSuffix));
  const size_t body_len = (sizeof(kPrefix) - 1) + nout + (sizeof(kSuffix) - 1);

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(60000);
  if (!http.begin(client, url)) {
    free(body);
    return false;
  }
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", String("Bearer ") + MYNAH_SUPABASE_ANON_KEY);
  http.addHeader("apikey", MYNAH_SUPABASE_ANON_KEY);

  const int code = http.POST(body, body_len);
  free(body);

  if (code != 200) {
    ESP_LOGW(TAG, "voice-pipeline HTTP %d", code);
    http.end();
    return false;
  }

  const size_t resp_cap = 512 * 1024;
  int respLen = http.getSize();
  if (respLen <= 0 || static_cast<size_t>(respLen) > resp_cap) {
    ESP_LOGW(TAG, "bad content-length %d", respLen);
    http.end();
    return false;
  }

  char *resp = static_cast<char *>(
      heap_caps_malloc(static_cast<size_t>(respLen) + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!resp) {
    resp = static_cast<char *>(malloc(static_cast<size_t>(respLen) + 1));
  }
  if (!resp) {
    http.end();
    return false;
  }
  WiFiClient *s = http.getStreamPtr();
  size_t rd = 0;
  while (rd < static_cast<size_t>(respLen) && s->connected()) {
    const int n = s->readBytes(resp + rd, static_cast<size_t>(respLen) - rd);
    if (n <= 0) {
      break;
    }
    rd += static_cast<size_t>(n);
  }
  resp[rd] = '\0';
  http.end();

  extract_json_string_field(resp, "transcript", r->transcript, sizeof(r->transcript));
  extract_json_string_field(resp, "reply", r->reply, sizeof(r->reply));
  if (!extract_audio_base64(resp, &r->mp3, &r->mp3_len)) {
    free(resp);
    return false;
  }
  free(resp);
  return r->mp3_len > 0;
}
