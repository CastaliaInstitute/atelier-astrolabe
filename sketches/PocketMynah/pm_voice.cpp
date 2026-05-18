#include "pm_voice.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <mbedtls/base64.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pm_config.h"
#include "pm_castalia_auth.h"
#include "pm_tls.h"

static const char *TAG = "pm_voice";

static constexpr uint32_t kVoiceNetTaskStack = 32768;
static constexpr uint32_t kVoiceHttpTimeoutMs = 120000;
/** voice-pipeline JSON + base64 MP3; long TTS replies exceed 512 KiB. */
static constexpr size_t kVoiceRespMaxBytes = 1536u * 1024u;

static TaskHandle_t s_voice_task = nullptr;
static volatile bool s_voice_done = false;
static volatile bool s_voice_ok = false;
static volatile PmVoiceStatus s_voice_status = PmVoiceStatus::Idle;
static volatile uint8_t s_voice_op = 0; /** 1 = message, 2 = pcm */
static volatile bool s_voice_cancel = false;
static uint32_t s_voice_started_ms = 0;

static const char *s_req_message = nullptr;
static const char *s_req_system = nullptr;
static const uint8_t *s_req_pcm = nullptr;
static size_t s_req_pcm_len = 0;
static PmVoiceResult *s_req_result = nullptr;

static char s_esc_msg[2048];
static char s_esc_sys[768];
static char s_esc_sys_pcm[3072];
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

/** True when the voice-pipeline JSON body looks complete (not truncated mid-field). */
static bool voice_response_json_complete(const char *buf, size_t len) {
  if (!buf || len < 8 || buf[0] != '{') {
    return false;
  }
  const char *k = "\"audioBase64\":\"";
  const char *p = strstr(buf, k);
  if (p) {
    p += strlen(k);
    while (p < buf + len && *p != '"') {
      ++p;
    }
    return p < buf + len && *p == '"';
  }
  for (size_t i = len; i > 0; --i) {
    const char c = buf[i - 1];
    if (c == '}') {
      return true;
    }
    if (c != ' ' && c != '\n' && c != '\r' && c != '\t') {
      break;
    }
  }
  return false;
}

static bool voice_result_ok(PmVoiceResult *r) {
  if (!r) {
    return false;
  }
  if (r->mp3 && r->mp3_len >= 64) {
    return true;
  }
  return r->reply[0] != '\0' || r->transcript[0] != '\0';
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
  if (declared > 0 && static_cast<size_t>(declared) > max_cap) {
    ESP_LOGW(TAG, "HTTP Content-Length %d exceeds cap %u", declared, static_cast<unsigned>(max_cap));
    return false;
  }

  char *buf = static_cast<char *>(
      heap_caps_malloc(max_cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!buf) {
    buf = static_cast<char *>(malloc(max_cap));
  }
  if (!buf) {
    ESP_LOGE(TAG, "OOM for HTTP body alloc %u", static_cast<unsigned>(max_cap));
    return false;
  }

  size_t rd = 0;
  const size_t read_cap = max_cap - 1u;
  const uint32_t deadline = millis() + 90000u;
  uint32_t last_rx_ms = 0;

  for (;;) {
    esp_task_wdt_reset();
    if (s_voice_cancel) {
      free(buf);
      return false;
    }
    if (static_cast<int32_t>(millis() - deadline) >= 0) {
      ESP_LOGW(TAG, "HTTP body read timeout (%u bytes)", static_cast<unsigned>(rd));
      break;
    }

    const int avail = stream->available();
    if (avail > 0) {
      const size_t take =
          static_cast<size_t>(avail) < (read_cap - rd) ? static_cast<size_t>(avail) : (read_cap - rd);
      if (take == 0) {
        break;
      }
      const int n = stream->readBytes(buf + rd, take);
      if (n > 0) {
        rd += static_cast<size_t>(n);
        last_rx_ms = millis();
        buf[rd] = '\0';
        if (voice_response_json_complete(buf, rd)) {
          ESP_LOGI(TAG, "HTTP body complete at %u bytes (declared %d)", static_cast<unsigned>(rd), declared);
          break;
        }
        if (rd >= read_cap) {
          break;
        }
        continue;
      }
    }

    if (rd > 0) {
      buf[rd] = '\0';
      if (voice_response_json_complete(buf, rd)) {
        break;
      }
      if (last_rx_ms != 0 && (millis() - last_rx_ms) > 2500u) {
        ESP_LOGW(TAG, "HTTP idle after %u bytes", static_cast<unsigned>(rd));
        break;
      }
    }

    if (!http->connected() && avail == 0) {
      break;
    }
    delay(5);
  }

  buf[rd] = '\0';
  if (rd == 0) {
    ESP_LOGW(TAG, "HTTP body empty (declared %d)", declared);
    free(buf);
    return false;
  }

  if (!voice_response_json_complete(buf, rd)) {
    const int extra = stream->available();
    ESP_LOGW(TAG, "HTTP incomplete JSON %u bytes (declared %d, +%d pending)", static_cast<unsigned>(rd), declared,
             extra);
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
  (void)pm_castalia_auth_prepare_for_voice();

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
  pm_tls_configure_client(client);
  HTTPClient http;
  http.setTimeout(static_cast<uint16_t>(kVoiceHttpTimeoutMs > 60000u ? 60000u : kVoiceHttpTimeoutMs));
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
    if (code == 401) {
      voice_set_error("sign in on Castalia face");
    } else {
      snprintf(s_last_error, sizeof(s_last_error), "HTTP %d", code);
    }
    http.end();
    return false;
  }

  const size_t resp_cap = kVoiceRespMaxBytes;
  const int declared_sz = http.getSize();
  char *resp = nullptr;
  if (!read_http_json_body(&http, &resp, resp_cap)) {
    ESP_LOGW(TAG, "voice-pipeline (message) body read fail (declared len %d)", declared_sz);
    voice_set_error(declared_sz > static_cast<int>(resp_cap) ? "reply too large" : "bad response");
    http.end();
    return false;
  }
  http.end();

  extract_json_string_field(resp, "transcript", r->transcript, sizeof(r->transcript));
  extract_json_string_field(resp, "reply", r->reply, sizeof(r->reply));
  if (!extract_audio_base64(resp, &r->mp3, &r->mp3_len)) {
    ESP_LOGI(TAG, "voice (message) text-only (no audioBase64)");
  } else {
    ESP_LOGI(TAG, "voice (message) mp3 %u bytes", static_cast<unsigned>(r->mp3_len));
  }
  free(resp);
  if (!voice_result_ok(r)) {
    voice_set_error("empty reply");
    return false;
  }
  voice_set_error(nullptr);
  return true;
}

static bool voice_post_pcm_inner(const uint8_t *pcm, size_t pcm_len, const char *system_instruction,
                                 PmVoiceResult *r) {
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
  (void)pm_castalia_auth_prepare_for_voice();

  char base[160];
  strncpy(base, MYNAH_SUPABASE_URL, sizeof(base) - 1);
  base[sizeof(base) - 1] = '\0';
  trim_supabase_url(base, sizeof(base));

  char url[224];
  snprintf(url, sizeof(url), "%s/functions/v1/voice-pipeline", base);

  static const char kPrefix[] =
      "{\"languageCode\":\"en-US\",\"sampleRateHertz\":16000,\"audioBase64\":\"";
  s_esc_sys_pcm[0] = '\0';
  const bool have_sys = system_instruction && system_instruction[0] != '\0';
  if (have_sys) {
    json_escape_string(system_instruction, s_esc_sys_pcm, sizeof(s_esc_sys_pcm));
  }

  const size_t b64max = ((pcm_len + 2) / 3) * 4 + 4;
  const size_t sys_extra = have_sys ? (24 + strlen(s_esc_sys_pcm)) : 0;
  const size_t body_cap = sizeof(kPrefix) - 1 + b64max + sys_extra + 4;
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
  size_t body_len = (sizeof(kPrefix) - 1) + nout;
  if (body_len + 2 > body_cap) {
    free(body);
    voice_set_error("body too large");
    return false;
  }
  body[body_len++] = '"';
  if (have_sys) {
    const int n = snprintf(reinterpret_cast<char *>(body + body_len), body_cap - body_len,
                           ",\"systemInstruction\":\"%s\"", s_esc_sys_pcm);
    if (n <= 0 || static_cast<size_t>(n) >= body_cap - body_len) {
      free(body);
      voice_set_error("body too large");
      return false;
    }
    body_len += static_cast<size_t>(n);
  }
  if (body_len + 1 > body_cap) {
    free(body);
    voice_set_error("body too large");
    return false;
  }
  body[body_len++] = '}';

  WiFiClientSecure client;
  pm_tls_configure_client(client);
  HTTPClient http;
  http.setTimeout(static_cast<uint16_t>(kVoiceHttpTimeoutMs > 60000u ? 60000u : kVoiceHttpTimeoutMs));
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
    if (code == 401) {
      voice_set_error("sign in on Castalia face");
    } else {
      snprintf(s_last_error, sizeof(s_last_error), "HTTP %d", code);
    }
    http.end();
    return false;
  }

  const size_t resp_cap = kVoiceRespMaxBytes;
  const int declared_sz = http.getSize();
  char *resp = nullptr;
  if (!read_http_json_body(&http, &resp, resp_cap)) {
    ESP_LOGW(TAG, "voice-pipeline body read fail (declared len %d)", declared_sz);
    voice_set_error(declared_sz > static_cast<int>(resp_cap) ? "reply too large" : "bad response");
    http.end();
    return false;
  }
  http.end();

  extract_json_string_field(resp, "transcript", r->transcript, sizeof(r->transcript));
  extract_json_string_field(resp, "reply", r->reply, sizeof(r->reply));
  if (!extract_audio_base64(resp, &r->mp3, &r->mp3_len)) {
    ESP_LOGI(TAG, "voice (pcm) text-only (no audioBase64)");
  } else {
    ESP_LOGI(TAG, "voice (pcm) mp3 %u bytes", static_cast<unsigned>(r->mp3_len));
  }
  free(resp);
  if (!voice_result_ok(r)) {
    voice_set_error("empty reply");
    return false;
  }
  voice_set_error(nullptr);
  return true;
}

static bool voice_post_clock_agenda_inner(PmVoiceResult *r) {
  if (!r) {
    voice_set_error("no result");
    return false;
  }
  memset(r->transcript, 0, sizeof(r->transcript));
  memset(r->reply, 0, sizeof(r->reply));
  r->mp3 = nullptr;
  r->mp3_len = 0;

  if (strlen(MYNAH_SUPABASE_URL) == 0 || strlen(MYNAH_SUPABASE_ANON_KEY) == 0) {
    voice_set_error("no supabase config");
    return false;
  }
  (void)pm_castalia_auth_prepare_for_voice();

  char base[160];
  strncpy(base, MYNAH_SUPABASE_URL, sizeof(base) - 1);
  base[sizeof(base) - 1] = '\0';
  trim_supabase_url(base, sizeof(base));

  char url[224];
  snprintf(url, sizeof(url), "%s/functions/v1/voice-pipeline", base);

  const time_t epoch = time(nullptr);
  char body[120];
  snprintf(body, sizeof(body),
           "{\"face\":\"clock_agenda\",\"epochSeconds\":%lld,\"skipLlm\":true}",
           static_cast<long long>(epoch));

  WiFiClientSecure client;
  pm_tls_configure_client(client);
  HTTPClient http;
  http.setTimeout(static_cast<uint16_t>(kVoiceHttpTimeoutMs > 60000u ? 60000u : kVoiceHttpTimeoutMs));
  if (!http.begin(client, url)) {
    voice_set_error("http begin");
    return false;
  }
  http.addHeader("Content-Type", "application/json");
  pm_castalia_auth_apply_headers(&http);

  const int code = http.POST(reinterpret_cast<uint8_t *>(body), strlen(body));
  if (code != 200) {
    ESP_LOGW(TAG, "voice-pipeline (clock_agenda) HTTP %d", code);
    voice_set_error(code == 401 ? "sign in on Castalia face" : "HTTP error");
    http.end();
    return false;
  }

  const size_t resp_cap = kVoiceRespMaxBytes;
  char *resp = nullptr;
  if (!read_http_json_body(&http, &resp, resp_cap)) {
    voice_set_error("bad response");
    http.end();
    return false;
  }
  http.end();

  extract_json_string_field(resp, "transcript", r->transcript, sizeof(r->transcript));
  extract_json_string_field(resp, "reply", r->reply, sizeof(r->reply));
  if (!extract_audio_base64(resp, &r->mp3, &r->mp3_len)) {
    ESP_LOGI(TAG, "voice (clock_agenda) text-only");
  } else {
    ESP_LOGI(TAG, "voice (clock_agenda) mp3 %u bytes", static_cast<unsigned>(r->mp3_len));
  }
  free(resp);
  if (!voice_result_ok(r)) {
    voice_set_error("empty agenda");
    return false;
  }
  voice_set_error(nullptr);
  return true;
}

static void voice_net_task(void *arg) {
  (void)arg;
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    const uint8_t op = s_voice_op;
    if (op == 1) {
      s_voice_ok = voice_post_message_inner(s_req_message, s_req_system, s_req_result);
    } else if (op == 2) {
      s_voice_ok = voice_post_pcm_inner(s_req_pcm, s_req_pcm_len, s_req_system, s_req_result);
    } else if (op == 3) {
      s_voice_ok = voice_post_clock_agenda_inner(s_req_result);
    } else {
      s_voice_ok = false;
    }
    if (s_voice_cancel) {
      s_voice_ok = false;
      if (s_req_result) {
        pm_voice_result_free(s_req_result);
      }
    }
    s_voice_status = s_voice_ok ? PmVoiceStatus::DoneOk : PmVoiceStatus::DoneFail;
    s_voice_done = true;
    s_voice_cancel = false;
  }
}

static void voice_net_task_ensure() {
  if (s_voice_task) {
    return;
  }
  xTaskCreatePinnedToCore(voice_net_task, "voice_net", kVoiceNetTaskStack, nullptr, 1, &s_voice_task, 1);
}

static bool voice_net_begin(uint8_t op) {
  voice_net_task_ensure();
  if (!s_voice_task) {
    return false;
  }
  if (s_voice_status == PmVoiceStatus::Working) {
    ESP_LOGW(TAG, "voice_begin while busy");
    return false;
  }
  s_voice_cancel = false;
  s_voice_started_ms = millis();
  s_voice_op = op;
  s_voice_done = false;
  s_voice_ok = false;
  s_voice_status = PmVoiceStatus::Working;
  xTaskNotify(s_voice_task, 1, eSetBits);
  return true;
}

static bool voice_net_run(uint8_t op, uint32_t timeout_ms) {
  if (!voice_net_begin(op)) {
    return false;
  }
  const uint32_t deadline = millis() + timeout_ms;
  while (pm_voice_poll() == PmVoiceStatus::Working) {
    delay(10);
    if (static_cast<int32_t>(millis() - deadline) >= 0) {
      voice_set_error("timeout");
      ESP_LOGW(TAG, "voice op %u timeout", static_cast<unsigned>(op));
      s_voice_status = PmVoiceStatus::DoneFail;
      return false;
    }
  }
  return s_voice_status == PmVoiceStatus::DoneOk;
}

bool pm_voice_begin_message(const char *message, const char *system_instruction, PmVoiceResult *r) {
  s_req_message = message;
  s_req_system = system_instruction;
  s_req_result = r;
  return voice_net_begin(1);
}

bool pm_voice_begin_pcm(const uint8_t *pcm, size_t pcm_len, const char *system_instruction, PmVoiceResult *r) {
  s_req_pcm = pcm;
  s_req_pcm_len = pcm_len;
  s_req_system = system_instruction;
  s_req_result = r;
  return voice_net_begin(2);
}

bool pm_voice_begin_clock_agenda(PmVoiceResult *r) {
  s_req_result = r;
  return voice_net_begin(3);
}

PmVoiceStatus pm_voice_poll(void) {
  return s_voice_status;
}

void pm_voice_abort(void) {
  s_voice_cancel = true;
  voice_set_error("timeout");
  s_voice_status = PmVoiceStatus::DoneFail;
  s_voice_done = true;
}

bool pm_voice_post_message(const char *message, const char *system_instruction, PmVoiceResult *r) {
  return voice_net_run(1, kVoiceHttpTimeoutMs + 5000u);
}

bool pm_voice_post_pcm(const uint8_t *pcm, size_t pcm_len, const char *system_instruction, PmVoiceResult *r) {
  s_req_pcm = pcm;
  s_req_pcm_len = pcm_len;
  s_req_system = system_instruction;
  s_req_result = r;
  return voice_net_run(2, kVoiceHttpTimeoutMs + 5000u);
}
