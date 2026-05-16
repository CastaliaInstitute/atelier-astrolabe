#include "pm_voice.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <mbedtls/base64.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pm_config.h"
#include "pm_castalia_auth.h"

static const char *TAG = "pm_voice";

static constexpr uint32_t kVoiceNetTaskStack = 32768;
static constexpr uint32_t kVoiceHttpTimeoutMs = 120000;

static TaskHandle_t s_voice_task = nullptr;
static volatile bool s_voice_done = false;
static volatile bool s_voice_ok = false;
static volatile uint8_t s_voice_op = 0; /** 1 = message, 2 = pcm */

static const char *s_req_message = nullptr;
static const char *s_req_system = nullptr;
static const uint8_t *s_req_pcm = nullptr;
static size_t s_req_pcm_len = 0;
static PmVoiceResult *s_req_result = nullptr;

static char s_esc_msg[2048];
static char s_esc_sys[768];
static char s_last_error[80] = "";

static void voice_set_error(const char *msg) {
  if (!msg) {
    s_last_error[0] = '\0';
    return;
  }
  strncpy(s_last_error, msg, sizeof(s_last_error) - 1);
  s_last_error[sizeof(s_last_error) - 1] = '\0';
}

const char *pm_voice_last_error() {
  return s_last_error[0] != '\0' ? s_last_error : "voice failed";
}

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

static bool read_http_json_body(HTTPClient *http, char **out_resp, size_t max_cap) {
  if (!http || !out_resp || max_cap < 64) {
    return false;
  }
  *out_resp = nullptr;

  const int declared = http->getSize();
  WiFiClient *stream = http->getStreamPtr();
  if (!stream) {
    return false;
  }

  char *buf = static_cast<char *>(
      heap_caps_malloc(max_cap + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!buf) {
    buf = static_cast<char *>(malloc(max_cap + 1));
  }
  if (!buf) {
    return false;
  }

  size_t rd = 0;
  const uint32_t deadline = millis() + kVoiceHttpTimeoutMs;
  if (declared > 0 && static_cast<size_t>(declared) <= max_cap) {
    while (rd < static_cast<size_t>(declared)) {
      const int n = stream->readBytes(buf + rd, static_cast<size_t>(declared) - rd);
      if (n > 0) {
        rd += static_cast<size_t>(n);
        continue;
      }
      if (!http->connected() && stream->available() == 0) {
        break;
      }
      if (static_cast<int32_t>(millis() - deadline) >= 0) {
        break;
      }
      delay(1);
    }
  } else {
    for (;;) {
      const int avail = stream->available();
      if (avail > 0) {
        const size_t take =
            static_cast<size_t>(avail) < (max_cap - rd) ? static_cast<size_t>(avail) : (max_cap - rd);
        if (take == 0) {
          break;
        }
        const int n = stream->readBytes(buf + rd, take);
        if (n > 0) {
          rd += static_cast<size_t>(n);
          if (rd >= max_cap) {
            break;
          }
          continue;
        }
      }
      if (!http->connected() && stream->available() == 0) {
        break;
      }
      if (static_cast<int32_t>(millis() - deadline) >= 0) {
        break;
      }
      delay(2);
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

static size_t json_escape_string(const char *in, char *out, size_t out_cap) {
  if (!in || !out || out_cap < 4) {
    if (out && out_cap) {
      out[0] = '\0';
    }
    return 0;
  }
  size_t j = 0;
  for (size_t i = 0; in[i] != '\0' && j + 2 < out_cap; ++i) {
    const unsigned char c = static_cast<unsigned char>(in[i]);
    if (c == '"' || c == '\\') {
      out[j++] = '\\';
      out[j++] = static_cast<char>(c);
      continue;
    }
    if (c == '\n' || c == '\r' || c == '\t') {
      out[j++] = ' ';
      continue;
    }
    if (c < 32) {
      continue;
    }
    out[j++] = static_cast<char>(c);
  }
  out[j] = '\0';
  return j;
}

static bool voice_post_message_inner(const char *message, const char *system_instruction, PmVoiceResult *r) {
  if (!r || !message || message[0] == '\0') {
    voice_set_error("empty message");
    return false;
  }
  memset(r->transcript, 0, sizeof(r->transcript));
  memset(r->reply, 0, sizeof(r->reply));
  r->mp3 = nullptr;
  r->mp3_len = 0;

  if (strlen(MYNAH_SUPABASE_URL) == 0 || strlen(MYNAH_SUPABASE_ANON_KEY) == 0) {
    voice_set_error("no supabase config");
    ESP_LOGW(TAG, "Supabase URL or anon key empty");
    return false;
  }

  char base[160];
  strncpy(base, MYNAH_SUPABASE_URL, sizeof(base) - 1);
  base[sizeof(base) - 1] = '\0';
  trim_supabase_url(base, sizeof(base));

  char url[224];
  snprintf(url, sizeof(url), "%s/functions/v1/voice-pipeline", base);

  json_escape_string(message, s_esc_msg, sizeof(s_esc_msg));
  s_esc_sys[0] = '\0';
  if (system_instruction && system_instruction[0] != '\0') {
    json_escape_string(system_instruction, s_esc_sys, sizeof(s_esc_sys));
  }

  const bool have_sys = s_esc_sys[0] != '\0';
  const size_t body_cap = sizeof(s_esc_msg) + sizeof(s_esc_sys) + 160;
  char *body = static_cast<char *>(
      heap_caps_malloc(body_cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!body) {
    body = static_cast<char *>(malloc(body_cap));
  }
  if (!body) {
    voice_set_error("oom body");
    return false;
  }

  int n;
  if (have_sys) {
    n = snprintf(body, body_cap,
                 "{\"languageCode\":\"en-US\",\"message\":\"%s\",\"systemInstruction\":\"%s\"}",
                 s_esc_msg, s_esc_sys);
  } else {
    n = snprintf(body, body_cap, "{\"languageCode\":\"en-US\",\"message\":\"%s\"}", s_esc_msg);
  }
  if (n <= 0 || static_cast<size_t>(n) >= body_cap) {
    free(body);
    voice_set_error("body too large");
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(kVoiceHttpTimeoutMs);
  if (!http.begin(client, url)) {
    free(body);
    voice_set_error("http begin");
    return false;
  }
  http.addHeader("Content-Type", "application/json");
  pm_castalia_auth_apply_headers(&http);

  const int code = http.POST(reinterpret_cast<uint8_t *>(body), static_cast<size_t>(n));
  free(body);

  if (code != 200) {
    ESP_LOGW(TAG, "voice-pipeline HTTP %d (message)", code);
    snprintf(s_last_error, sizeof(s_last_error), "HTTP %d", code);
    http.end();
    return false;
  }

  const size_t resp_cap = 512 * 1024;
  const int declared_sz = http.getSize();
  char *resp = nullptr;
  if (!read_http_json_body(&http, &resp, resp_cap)) {
    ESP_LOGW(TAG, "voice-pipeline (message) empty body (declared len %d)", declared_sz);
    voice_set_error("empty response");
    http.end();
    return false;
  }
  http.end();

  extract_json_string_field(resp, "transcript", r->transcript, sizeof(r->transcript));
  extract_json_string_field(resp, "reply", r->reply, sizeof(r->reply));
  if (!extract_audio_base64(resp, &r->mp3, &r->mp3_len)) {
    ESP_LOGW(TAG, "voice-pipeline (message) missing audioBase64");
    voice_set_error("no audio");
    free(resp);
    return false;
  }
  free(resp);
  voice_set_error(nullptr);
  return r->mp3_len > 0;
}

static bool voice_post_pcm_inner(const uint8_t *pcm, size_t pcm_len, PmVoiceResult *r) {
  if (!r || !pcm || pcm_len == 0) {
    voice_set_error("empty pcm");
    return false;
  }
  memset(r->transcript, 0, sizeof(r->transcript));
  memset(r->reply, 0, sizeof(r->reply));
  r->mp3 = nullptr;
  r->mp3_len = 0;

  if (strlen(MYNAH_SUPABASE_URL) == 0 || strlen(MYNAH_SUPABASE_ANON_KEY) == 0) {
    voice_set_error("no supabase config");
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
    voice_set_error("oom body");
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
    voice_set_error("b64 encode");
    return false;
  }
  memcpy(body + sizeof(kPrefix) - 1 + nout, kSuffix, sizeof(kSuffix));
  const size_t body_len = (sizeof(kPrefix) - 1) + nout + (sizeof(kSuffix) - 1);

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(kVoiceHttpTimeoutMs);
  if (!http.begin(client, url)) {
    free(body);
    voice_set_error("http begin");
    return false;
  }
  http.addHeader("Content-Type", "application/json");
  pm_castalia_auth_apply_headers(&http);

  const int code = http.POST(body, body_len);
  free(body);

  if (code != 200) {
    ESP_LOGW(TAG, "voice-pipeline HTTP %d", code);
    snprintf(s_last_error, sizeof(s_last_error), "HTTP %d", code);
    http.end();
    return false;
  }

  const size_t resp_cap = 512 * 1024;
  const int declared_sz = http.getSize();
  char *resp = nullptr;
  if (!read_http_json_body(&http, &resp, resp_cap)) {
    ESP_LOGW(TAG, "voice-pipeline empty body (declared len %d)", declared_sz);
    voice_set_error("empty response");
    http.end();
    return false;
  }
  http.end();

  extract_json_string_field(resp, "transcript", r->transcript, sizeof(r->transcript));
  extract_json_string_field(resp, "reply", r->reply, sizeof(r->reply));
  if (!extract_audio_base64(resp, &r->mp3, &r->mp3_len)) {
    ESP_LOGW(TAG, "voice-pipeline missing audioBase64");
    voice_set_error("no audio");
    free(resp);
    return false;
  }
  free(resp);
  voice_set_error(nullptr);
  return r->mp3_len > 0;
}

static void voice_net_task(void *arg) {
  (void)arg;
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    const uint8_t op = s_voice_op;
    if (op == 1) {
      s_voice_ok = voice_post_message_inner(s_req_message, s_req_system, s_req_result);
    } else if (op == 2) {
      s_voice_ok = voice_post_pcm_inner(s_req_pcm, s_req_pcm_len, s_req_result);
    } else {
      s_voice_ok = false;
    }
    s_voice_done = true;
  }
}

static void voice_net_task_ensure() {
  if (s_voice_task) {
    return;
  }
  xTaskCreatePinnedToCore(voice_net_task, "voice_net", kVoiceNetTaskStack, nullptr, 1, &s_voice_task, 1);
}

static bool voice_net_run(uint8_t op, uint32_t timeout_ms) {
  voice_net_task_ensure();
  if (!s_voice_task) {
    return false;
  }
  s_voice_op = op;
  s_voice_done = false;
  xTaskNotify(s_voice_task, 1, eSetBits);
  const uint32_t deadline = millis() + timeout_ms;
  while (!s_voice_done) {
    delay(10);
    if (static_cast<int32_t>(millis() - deadline) >= 0) {
      voice_set_error("timeout");
      ESP_LOGW(TAG, "voice op %u timeout", static_cast<unsigned>(op));
      return false;
    }
  }
  return s_voice_ok;
}

bool pm_voice_post_message(const char *message, const char *system_instruction, PmVoiceResult *r) {
  s_req_message = message;
  s_req_system = system_instruction;
  s_req_result = r;
  return voice_net_run(1, kVoiceHttpTimeoutMs + 5000u);
}

bool pm_voice_post_pcm(const uint8_t *pcm, size_t pcm_len, PmVoiceResult *r) {
  s_req_pcm = pcm;
  s_req_pcm_len = pcm_len;
  s_req_result = r;
  return voice_net_run(2, kVoiceHttpTimeoutMs + 5000u);
}
