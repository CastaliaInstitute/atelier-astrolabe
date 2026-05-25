#include "p4_voice.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "mbedtls/base64.h"
#include "p4_audio.h"

#if __has_include("secrets.local.h")
#include "secrets.local.h"
#else
#include "secrets.example.h"
#endif

static const char *TAG = "astrolabe_voice";
static const size_t VOICE_RESP_MAX = 1536u * 1024u;
static char s_last_error[96] = "";

static void set_error(const char *msg) {
  if (msg == NULL) {
    s_last_error[0] = '\0';
    return;
  }
  strlcpy(s_last_error, msg, sizeof(s_last_error));
}

const char *astrolabe_p4_voice_last_error(void) {
  return s_last_error[0] != '\0' ? s_last_error : "voice failed";
}

void astrolabe_p4_voice_result_free(astrolabe_p4_voice_result_t *result) {
  if (result == NULL) {
    return;
  }
  free(result->mp3);
  result->mp3 = NULL;
  result->mp3_len = 0;
}

static void trim_url(char *url) {
  while (url[0] != '\0' && url[strlen(url) - 1] == '/') {
    url[strlen(url) - 1] = '\0';
  }
}

static size_t json_escape(const char *in, char *out, size_t cap) {
  if (out == NULL || cap == 0) {
    return 0;
  }
  size_t j = 0;
  if (in == NULL) {
    out[0] = '\0';
    return 0;
  }
  for (size_t i = 0; in[i] != '\0' && j + 2 < cap; ++i) {
    unsigned char c = (unsigned char)in[i];
    if (c == '"' || c == '\\') {
      out[j++] = '\\';
      out[j++] = (char)c;
    } else if (c == '\n' || c == '\r' || c == '\t') {
      out[j++] = ' ';
    } else if (c >= 32) {
      out[j++] = (char)c;
    }
  }
  out[j] = '\0';
  return j;
}

static bool json_field(const char *json, const char *key, char *out, size_t cap) {
  char pat[48];
  snprintf(pat, sizeof(pat), "\"%s\":\"", key);
  const char *p = strstr(json, pat);
  if (p == NULL) {
    return false;
  }
  p += strlen(pat);
  size_t o = 0;
  while (*p != '\0' && *p != '"' && o + 1 < cap) {
    if (*p == '\\' && p[1] != '\0') {
      ++p;
    }
    out[o++] = *p++;
  }
  out[o] = '\0';
  return true;
}

static bool extract_audio(const char *json, uint8_t **out_mp3, size_t *out_len) {
  const char *k = "\"audioBase64\":\"";
  const char *p = strstr(json, k);
  if (p == NULL) {
    return false;
  }
  p += strlen(k);
  const char *start = p;
  while (*p != '\0' && *p != '"') {
    ++p;
  }
  const size_t b64_len = (size_t)(p - start);
  if (b64_len == 0) {
    return false;
  }
  const size_t cap = (b64_len * 3u) / 4u + 8u;
  uint8_t *buf = heap_caps_malloc(cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (buf == NULL) {
    buf = malloc(cap);
  }
  if (buf == NULL) {
    return false;
  }
  size_t olen = 0;
  if (mbedtls_base64_decode(buf, cap, &olen, (const unsigned char *)start, b64_len) != 0) {
    free(buf);
    return false;
  }
  *out_mp3 = buf;
  *out_len = olen;
  return true;
}

static bool response_complete(const char *buf, size_t len) {
  if (buf == NULL || len < 2 || buf[0] != '{') {
    return false;
  }
  const char *audio = strstr(buf, "\"audioBase64\":\"");
  if (audio != NULL) {
    const char *p = audio + strlen("\"audioBase64\":\"");
    while (p < buf + len && *p != '"') {
      ++p;
    }
    return p < buf + len && *p == '"';
  }
  for (size_t i = len; i > 0; --i) {
    char c = buf[i - 1];
    if (c == '}') {
      return true;
    }
    if (c != ' ' && c != '\r' && c != '\n' && c != '\t') {
      break;
    }
  }
  return false;
}

static bool voice_url(char *out, size_t cap) {
  if (MYNAH_SUPABASE_URL[0] == '\0' || MYNAH_SUPABASE_ANON_KEY[0] == '\0') {
    set_error("missing MYNAH_SUPABASE_*");
    return false;
  }
  char base[180];
  strlcpy(base, MYNAH_SUPABASE_URL, sizeof(base));
  trim_url(base);
  int n = snprintf(out, cap, "%s/functions/v1/voice-pipeline", base);
  return n > 0 && (size_t)n < cap;
}

static bool post_json(const uint8_t *body, size_t body_len, astrolabe_p4_voice_result_t *result) {
  char url[240];
  if (!voice_url(url, sizeof(url))) {
    return false;
  }
  memset(result, 0, sizeof(*result));

  esp_http_client_config_t config = {
      .url = url,
      .timeout_ms = 120000,
      .buffer_size = 2048,
      .buffer_size_tx = 2048,
      .crt_bundle_attach = esp_crt_bundle_attach,
  };
  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == NULL) {
    set_error("http init");
    return false;
  }

  esp_http_client_set_method(client, HTTP_METHOD_POST);
  esp_http_client_set_header(client, "Content-Type", "application/json");
  esp_http_client_set_header(client, "apikey", MYNAH_SUPABASE_ANON_KEY);
  char auth[260];
  snprintf(auth, sizeof(auth), "Bearer %s", MYNAH_SUPABASE_ANON_KEY);
  esp_http_client_set_header(client, "Authorization", auth);
  esp_http_client_set_header(client, "User-Agent", "Astrolabe-P4/1.0");

  esp_err_t ret = esp_http_client_open(client, (int)body_len);
  if (ret == ESP_OK) {
    int written = esp_http_client_write(client, (const char *)body, (int)body_len);
    if (written != (int)body_len) {
      ret = ESP_FAIL;
    }
  }
  if (ret == ESP_OK) {
    (void)esp_http_client_fetch_headers(client);
  }
  int status = esp_http_client_get_status_code(client);
  if (ret != ESP_OK || status != 200) {
    snprintf(s_last_error, sizeof(s_last_error), "HTTP %d %s", status, esp_err_to_name(ret));
    ESP_LOGW(TAG, "voice-pipeline failed status=%d err=%s", status, esp_err_to_name(ret));
    esp_http_client_cleanup(client);
    return false;
  }

  char *resp = heap_caps_malloc(VOICE_RESP_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (resp == NULL) {
    resp = malloc(VOICE_RESP_MAX);
  }
  if (resp == NULL) {
    esp_http_client_cleanup(client);
    set_error("oom response");
    return false;
  }
  size_t rd = 0;
  while (rd + 1 < VOICE_RESP_MAX) {
    int n = esp_http_client_read(client, resp + rd, (int)((VOICE_RESP_MAX - 1u) - rd));
    if (n <= 0) {
      break;
    }
    rd += (size_t)n;
    resp[rd] = '\0';
    if (response_complete(resp, rd)) {
      break;
    }
  }
  esp_http_client_cleanup(client);
  resp[rd] = '\0';
  if (rd == 0 || !response_complete(resp, rd)) {
    free(resp);
    set_error("bad response");
    return false;
  }

  json_field(resp, "transcript", result->transcript, sizeof(result->transcript));
  json_field(resp, "reply", result->reply, sizeof(result->reply));
  if (extract_audio(resp, &result->mp3, &result->mp3_len)) {
    ESP_LOGI(TAG, "voice audio mp3=%u bytes", (unsigned)result->mp3_len);
  } else {
    ESP_LOGI(TAG, "voice text-only response");
  }
  free(resp);
  if (result->reply[0] == '\0' && result->transcript[0] == '\0' && result->mp3_len == 0) {
    set_error("empty reply");
    return false;
  }
  set_error(NULL);
  return true;
}

bool astrolabe_p4_voice_post_message(const char *message, const char *system_instruction,
                                     astrolabe_p4_voice_result_t *result) {
  if (message == NULL || message[0] == '\0' || result == NULL) {
    set_error("empty message");
    return false;
  }
  char esc_msg[2048];
  char esc_sys[1024];
  json_escape(message, esc_msg, sizeof(esc_msg));
  json_escape(system_instruction, esc_sys, sizeof(esc_sys));
  size_t cap = strlen(esc_msg) + strlen(esc_sys) + 160;
  char *body = heap_caps_malloc(cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (body == NULL) {
    body = malloc(cap);
  }
  if (body == NULL) {
    set_error("oom body");
    return false;
  }
  int n = esc_sys[0] != '\0'
              ? snprintf(body, cap, "{\"languageCode\":\"en-US\",\"message\":\"%s\",\"systemInstruction\":\"%s\"}",
                         esc_msg, esc_sys)
              : snprintf(body, cap, "{\"languageCode\":\"en-US\",\"message\":\"%s\"}", esc_msg);
  bool ok = n > 0 && (size_t)n < cap && post_json((const uint8_t *)body, (size_t)n, result);
  free(body);
  return ok;
}

bool astrolabe_p4_voice_post_pcm(const int16_t *pcm, size_t samples, int sample_rate,
                                 const char *system_instruction, astrolabe_p4_voice_result_t *result) {
  if (pcm == NULL || samples == 0 || result == NULL) {
    set_error("empty pcm");
    return false;
  }
  const size_t pcm_len = samples * sizeof(int16_t);
  const size_t b64max = ((pcm_len + 2u) / 3u) * 4u + 8u;
  char esc_sys[2048];
  json_escape(system_instruction, esc_sys, sizeof(esc_sys));
  const bool have_sys = esc_sys[0] != '\0';
  const size_t cap = b64max + strlen(esc_sys) + 160u;
  uint8_t *body = heap_caps_malloc(cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (body == NULL) {
    body = malloc(cap);
  }
  if (body == NULL) {
    set_error("oom body");
    return false;
  }

  int n = snprintf((char *)body, cap, "{\"languageCode\":\"en-US\",\"sampleRateHertz\":%d,\"audioBase64\":\"",
                   sample_rate);
  if (n <= 0 || (size_t)n >= cap) {
    free(body);
    set_error("body too large");
    return false;
  }
  size_t body_len = (size_t)n;
  size_t nout = 0;
  if (mbedtls_base64_encode(body + body_len, cap - body_len, &nout, (const unsigned char *)pcm, pcm_len) != 0) {
    free(body);
    set_error("b64 encode");
    return false;
  }
  body_len += nout;
  body[body_len++] = '"';
  if (have_sys) {
    n = snprintf((char *)body + body_len, cap - body_len, ",\"systemInstruction\":\"%s\"", esc_sys);
    if (n <= 0 || (size_t)n >= cap - body_len) {
      free(body);
      set_error("body too large");
      return false;
    }
    body_len += (size_t)n;
  }
  body[body_len++] = '}';
  bool ok = post_json(body, body_len, result);
  free(body);
  return ok;
}

bool astrolabe_p4_voice_speak_message(const char *message, const char *system_instruction) {
  astrolabe_p4_voice_result_t result = {};
  bool ok = astrolabe_p4_voice_post_message(message, system_instruction, &result);
  if (ok) {
    ESP_LOGI(TAG, "voice reply: %s", result.reply);
    if (result.mp3 != NULL && result.mp3_len > 0) {
      ok = astrolabe_p4_audio_play_mp3(result.mp3, result.mp3_len);
    }
  }
  astrolabe_p4_voice_result_free(&result);
  return ok;
}
