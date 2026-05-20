#include "pm_commonplace.h"

#include <HTTPClient.h>
#include <LittleFS.h>
#include <WiFiClientSecure.h>
#include <mbedtls/base64.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pm_castalia_auth.h"
#include "pm_config.h"

static const char *TAG = "pm_commonplace";

static constexpr uint32_t kCommonplaceTaskStack = 32768;
static constexpr size_t kRespMaxBytes = 8192;

static TaskHandle_t s_task = nullptr;
static volatile bool s_done = false;
static volatile bool s_ok = false;
static volatile PmCommonplaceStatus s_status = PmCommonplaceStatus::Idle;
static volatile bool s_cancel = false;

static const uint8_t *s_req_pcm = nullptr;
static size_t s_req_pcm_len = 0;

static char s_last_error[80] = "";
static char s_last_transcript[512] = "";
static bool s_fs_ready = false;

static void set_error(const char *msg);

static bool note_fs_begin(void) {
  if (s_fs_ready) {
    return true;
  }
  if (!LittleFS.begin(true)) {
    set_error("flash fs");
    return false;
  }
  if (!LittleFS.exists("/notes")) {
    (void)LittleFS.mkdir("/notes");
  }
  s_fs_ready = true;
  return true;
}

static void set_error(const char *msg) {
  if (!msg) {
    s_last_error[0] = '\0';
    return;
  }
  strncpy(s_last_error, msg, sizeof(s_last_error) - 1);
  s_last_error[sizeof(s_last_error) - 1] = '\0';
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

static bool extract_json_bool_field(const char *json, const char *key, bool *out) {
  char pat[32];
  snprintf(pat, sizeof(pat), "\"%s\":", key);
  const char *p = strstr(json, pat);
  if (!p) {
    return false;
  }
  p += strlen(pat);
  while (*p == ' ') {
    ++p;
  }
  if (strncmp(p, "true", 4) == 0) {
    *out = true;
    return true;
  }
  if (strncmp(p, "false", 5) == 0) {
    *out = false;
    return true;
  }
  return false;
}

static void trim_supabase_url(char *url, size_t cap) {
  if (!url || cap == 0) {
    return;
  }
  while (strlen(url) > 0 && url[strlen(url) - 1] == '/') {
    url[strlen(url) - 1] = '\0';
  }
}

static bool read_small_json_body(HTTPClient *http, char **out_resp) {
  WiFiClient *stream = http->getStreamPtr();
  if (!stream) {
    return false;
  }
  char *buf = static_cast<char *>(heap_caps_malloc(kRespMaxBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!buf) {
    buf = static_cast<char *>(malloc(kRespMaxBytes));
  }
  if (!buf) {
    return false;
  }
  size_t rd = 0;
  const uint32_t deadline = millis() + 60000u;
  while (rd < kRespMaxBytes - 1 && static_cast<int32_t>(millis() - deadline) < 0) {
    const int avail = stream->available();
    if (avail > 0) {
      const size_t take = static_cast<size_t>(avail) < (kRespMaxBytes - 1 - rd)
                              ? static_cast<size_t>(avail)
                              : (kRespMaxBytes - 1 - rd);
      const int n = stream->readBytes(buf + rd, take);
      if (n > 0) {
        rd += static_cast<size_t>(n);
      }
    } else if (!http->connected()) {
      break;
    } else {
      delay(5);
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

static bool post_pcm_journal_inner(const uint8_t *pcm, size_t pcm_len) {
  s_last_transcript[0] = '\0';
  if (!pcm || pcm_len == 0) {
    set_error("empty pcm");
    return false;
  }
  if (strlen(MYNAH_SUPABASE_URL) == 0 || strlen(MYNAH_SUPABASE_ANON_KEY) == 0) {
    set_error("no supabase config");
    return false;
  }
  if (!pm_castalia_auth_prepare_for_voice()) {
    set_error("sign in on Castalia face");
    return false;
  }

  char base[160];
  strncpy(base, MYNAH_SUPABASE_URL, sizeof(base) - 1);
  base[sizeof(base) - 1] = '\0';
  trim_supabase_url(base, sizeof(base));

  char url[240];
  snprintf(url, sizeof(url), "%s/functions/v1/mynah-pocket-journal", base);

  static const char kPrefix[] =
      "{\"languageCode\":\"en-US\",\"sampleRateHertz\":16000,\"deviceLabel\":\"Astrolabe\","
      "\"audioBase64\":\"";
  const size_t b64max = ((pcm_len + 2) / 3) * 4 + 8;
  const size_t body_cap = sizeof(kPrefix) - 1 + b64max + 4;
  uint8_t *body = static_cast<uint8_t *>(
      heap_caps_malloc(body_cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!body) {
    body = static_cast<uint8_t *>(malloc(body_cap));
  }
  if (!body) {
    set_error("oom body");
    return false;
  }
  memcpy(body, kPrefix, sizeof(kPrefix) - 1);
  size_t nout = 0;
  if (mbedtls_base64_encode(body + sizeof(kPrefix) - 1, body_cap - (sizeof(kPrefix) - 1), &nout, pcm,
                            pcm_len) != 0) {
    free(body);
    set_error("b64 encode");
    return false;
  }
  size_t body_len = (sizeof(kPrefix) - 1) + nout;
  body[body_len++] = '"';
  body[body_len++] = '}';

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(120);
  HTTPClient http;
  http.setTimeout(65535);
  if (!http.begin(client, url)) {
    free(body);
    set_error("http begin");
    return false;
  }
  http.addHeader("Content-Type", "application/json");
  pm_castalia_auth_apply_headers(&http);

  Serial.printf("pm_commonplace: journal POST pcm=%u B\n", static_cast<unsigned>(pcm_len));
  const int code = http.POST(body, body_len);
  free(body);

  if (code != 200) {
    ESP_LOGW(TAG, "mynah-pocket-journal HTTP %d", code);
    if (code == 401) {
      set_error("sign in on Castalia face");
    } else if (code == 422) {
      set_error("no speech heard");
    } else {
      char errbuf[24];
      snprintf(errbuf, sizeof(errbuf), "HTTP %d", code);
      set_error(errbuf);
    }
    http.end();
    return false;
  }

  char *resp = nullptr;
  if (!read_small_json_body(&http, &resp)) {
    http.end();
    set_error("bad response");
    return false;
  }
  http.end();

  bool ok = false;
  (void)extract_json_bool_field(resp, "ok", &ok);
  if (!extract_json_string_field(resp, "transcript", s_last_transcript, sizeof(s_last_transcript))) {
    free(resp);
    set_error("bad response");
    return false;
  }
  free(resp);

  if (!ok || s_last_transcript[0] == '\0') {
    set_error("save failed");
    return false;
  }

  Serial.printf("pm_commonplace: saved journal (%.80s%s)\n", s_last_transcript,
                strlen(s_last_transcript) > 80 ? "…" : "");
  set_error(nullptr);
  return true;
}

static void commonplace_net_task(void *arg) {
  (void)arg;
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    s_ok = post_pcm_journal_inner(s_req_pcm, s_req_pcm_len);
    if (s_cancel) {
      s_ok = false;
    }
    s_status = s_ok ? PmCommonplaceStatus::DoneOk : PmCommonplaceStatus::DoneFail;
    s_done = true;
    s_cancel = false;
  }
}

static void task_ensure() {
  if (s_task) {
    return;
  }
  xTaskCreatePinnedToCore(commonplace_net_task, "commonplace_net", kCommonplaceTaskStack, nullptr, 1,
                          &s_task, 1);
}

PmCommonplaceStatus pm_commonplace_poll(void) {
  return s_status;
}

const char *pm_commonplace_last_transcript(void) {
  return s_last_transcript;
}

const char *pm_commonplace_last_error(void) {
  return s_last_error[0] ? s_last_error : "journal failed";
}

void pm_commonplace_abort(void) {
  s_cancel = true;
  if (s_last_error[0] == '\0') {
    set_error("cancelled");
  }
  s_status = PmCommonplaceStatus::DoneFail;
  s_done = true;
}

bool pm_commonplace_save_offline_note(const uint8_t *pcm, size_t pcm_len) {
  if (!pcm || pcm_len == 0) {
    set_error("empty pcm");
    return false;
  }
  if (!note_fs_begin()) {
    return false;
  }
  char path[48];
  snprintf(path, sizeof(path), "/notes/note-%lu.mp3", static_cast<unsigned long>(millis()));
  File f = LittleFS.open(path, FILE_WRITE);
  if (!f) {
    set_error("flash open");
    return false;
  }
  const size_t written = f.write(pcm, pcm_len);
  f.close();
  if (written != pcm_len) {
    (void)LittleFS.remove(path);
    set_error("flash write");
    return false;
  }
  Serial.printf("pm_commonplace: queued offline note %s bytes=%u\n", path,
                static_cast<unsigned>(pcm_len));
  set_error(nullptr);
  return true;
}

size_t pm_commonplace_offline_note_count(void) {
  if (!note_fs_begin()) {
    return 0;
  }
  File dir = LittleFS.open("/notes");
  if (!dir || !dir.isDirectory()) {
    return 0;
  }
  size_t n = 0;
  File f = dir.openNextFile();
  while (f) {
    if (!f.isDirectory()) {
      ++n;
    }
    f = dir.openNextFile();
  }
  return n;
}

bool pm_commonplace_begin_pcm_journal(const uint8_t *pcm, size_t pcm_len) {
  if (s_status == PmCommonplaceStatus::Working) {
    return false;
  }
  task_ensure();
  if (!s_task) {
    return false;
  }
  s_req_pcm = pcm;
  s_req_pcm_len = pcm_len;
  s_cancel = false;
  s_done = false;
  s_ok = false;
  s_status = PmCommonplaceStatus::Working;
  xTaskNotify(s_task, 1, eSetBits);
  return true;
}
