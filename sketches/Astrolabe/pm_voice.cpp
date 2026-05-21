#include "pm_voice.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <mbedtls/base64.h>
#include <mbedtls/platform.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "pm_config.h"
#include "pm_castalia_auth.h"
#include "pm_daily_briefing.h"
#include "pm_geo_tz.h"
#include "pm_speaker.h"
#include "pm_wifi_ntp.h"

static const char *TAG = "pm_voice";

static constexpr uint32_t kVoiceNetTaskStack = 16384;
/** STT + Gemini + TTS + large chunked JSON (astrology readings). */
static constexpr uint32_t kVoiceHttpTimeoutMs = 660000;
/** voice-pipeline JSON + base64 MP3. */
static constexpr size_t kVoiceRespMaxBytes = 2560u * 1024u;
static TaskHandle_t s_voice_task = nullptr;
static bool s_voice_task_with_caps = false;
static volatile bool s_voice_done = false;
static volatile bool s_voice_ok = false;
static volatile PmVoiceStatus s_voice_status = PmVoiceStatus::Idle;
static volatile bool s_voice_task_active = false;
static volatile uint8_t s_voice_op = 0; /** 1 = message, 2 = pcm, 3 = clock_agenda, 4 = daily_briefing */
static volatile bool s_voice_cancel = false;
static volatile bool s_daily_briefing_streamed = false;
static volatile bool s_daily_briefing_streaming_play = false;
static uint32_t s_voice_started_ms = 0;

static const char *s_req_message = nullptr;
static const char *s_req_system = nullptr;
static const char *s_req_face = nullptr;
static const char *s_req_faculty_slug = nullptr;
static const char *s_req_faculty_name = nullptr;
static const uint8_t *s_req_pcm = nullptr;
static size_t s_req_pcm_len = 0;
static PmVoiceResult *s_req_result = nullptr;

static char s_last_error[80] = "";
static const char *s_body_read_err = "bad response";
static bool s_voice_mbedtls_psram_ready = false;
static const char *kVoiceMp3Headers[] = {
    "X-Voice-Reply",
    "X-Voice-Transcript",
    "X-Voice-Route",
    "X-Voice-Tts-Chars",
};

static void voice_set_error(const char *msg);

static void voice_task_wdt_reset() {
  if (esp_task_wdt_status(nullptr) == ESP_OK) {
    esp_task_wdt_reset();
  }
}

static void *voice_mbedtls_calloc(size_t n, size_t size) {
  if (n == 0 || size == 0) {
    return nullptr;
  }
  if (size != 0 && n > SIZE_MAX / size) {
    return nullptr;
  }
  const size_t bytes = n * size;
  constexpr size_t kPreferPsramThreshold = 64;
  void *p = nullptr;
  if (bytes >= kPreferPsramThreshold) {
    p = heap_caps_calloc(n, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p) {
      return p;
    }
    return heap_caps_calloc(n, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  }
  p = heap_caps_calloc(n, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (p) {
    return p;
  }
  return heap_caps_calloc(n, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

static void voice_mbedtls_free(void *p) {
  heap_caps_free(p);
}

static void voice_prepare_mbedtls_psram(void) {
  if (s_voice_mbedtls_psram_ready) {
    return;
  }
  const int rc = mbedtls_platform_set_calloc_free(voice_mbedtls_calloc, voice_mbedtls_free);
  s_voice_mbedtls_psram_ready = rc == 0;
  Serial.printf("voice: mbedtls psram allocator %s rc=%d\n", s_voice_mbedtls_psram_ready ? "on" : "fail", rc);
}

static bool voice_pipeline_host(char *out, size_t out_cap) {
  if (!out || out_cap == 0 || strlen(MYNAH_SUPABASE_URL) == 0) {
    return false;
  }
  const char *p = strstr(MYNAH_SUPABASE_URL, "://");
  p = p ? p + 3 : MYNAH_SUPABASE_URL;
  size_t len = 0;
  while (p[len] && p[len] != '/' && p[len] != ':' && len + 1 < out_cap) {
    ++len;
  }
  if (len == 0 || len + 1 >= out_cap) {
    return false;
  }
  memcpy(out, p, len);
  out[len] = '\0';
  return true;
}

static bool voice_dns_probe(const char *host, IPAddress *out_ip) {
  if (!pm_wifi_connected() || !host || host[0] == '\0') {
    voice_set_error(pm_wifi_connected() ? "dns host" : "no wifi");
    return false;
  }
  IPAddress ip;
  const bool ok = WiFi.hostByName(host, ip) == 1 && ip != IPAddress(0, 0, 0, 0) &&
                  ip != IPAddress(255, 255, 255, 255);
  if (ok && out_ip) {
    *out_ip = ip;
  }
  return ok;
}

static void voice_set_fallback_dns(void) {
  esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  if (!netif) {
    return;
  }
  esp_netif_dns_info_t dns = {};
  dns.ip.type = ESP_IPADDR_TYPE_V4;
  dns.ip.u_addr.ip4.addr = static_cast<uint32_t>(IPAddress(1, 1, 1, 1));
  (void)esp_netif_set_dns_info(netif, ESP_NETIF_DNS_MAIN, &dns);
  dns.ip.u_addr.ip4.addr = static_cast<uint32_t>(IPAddress(8, 8, 8, 8));
  (void)esp_netif_set_dns_info(netif, ESP_NETIF_DNS_BACKUP, &dns);
}

bool pm_voice_pipeline_host_ready(bool recover) {
  char host[96];
  if (!voice_pipeline_host(host, sizeof(host))) {
    voice_set_error("no supabase host");
    return false;
  }

  IPAddress ip;
  if (voice_dns_probe(host, &ip)) {
    Serial.printf("voice: DNS ok %s -> %s\n", host, ip.toString().c_str());
    voice_set_error(nullptr);
    return true;
  }

  Serial.printf("voice: DNS fail %s\n", host);
  if (!recover) {
    voice_set_error("dns fail");
    return false;
  }

  Serial.println("voice: DNS recovery set 1.1.1.1/8.8.8.8");
  voice_set_fallback_dns();
  delay(250);
  if (voice_dns_probe(host, &ip)) {
    Serial.printf("voice: DNS recovered %s -> %s\n", host, ip.toString().c_str());
    voice_set_error(nullptr);
    return true;
  }

  Serial.println("voice: DNS recovery reconnect");
  if (pm_wifi_reconnect()) {
    voice_set_fallback_dns();
    delay(250);
    if (voice_dns_probe(host, &ip)) {
      Serial.printf("voice: DNS recovered %s -> %s\n", host, ip.toString().c_str());
      voice_set_error(nullptr);
      return true;
    }
  }

  voice_set_error("dns fail");
  return false;
}

static void body_read_set_err(const char *msg) {
  s_body_read_err = msg ? msg : "bad response";
}

static void voice_begin_http(WiFiClientSecure *client, HTTPClient *http) {
  voice_prepare_mbedtls_psram();
  client->setInsecure();
  client->setTimeout(360);
  http->setTimeout(65535);
}

static int hex_nibble(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  return -1;
}

static void copy_percent_decoded(const String &in, char *out, size_t out_cap) {
  if (!out || out_cap == 0) {
    return;
  }
  size_t o = 0;
  for (size_t i = 0; i < static_cast<size_t>(in.length()) && o + 1 < out_cap; ++i) {
    const char c = in.charAt(i);
    if (c == '%' && i + 2 < static_cast<size_t>(in.length())) {
      const int hi = hex_nibble(in.charAt(i + 1));
      const int lo = hex_nibble(in.charAt(i + 2));
      if (hi >= 0 && lo >= 0) {
        out[o++] = static_cast<char>((hi << 4) | lo);
        i += 2;
        continue;
      }
    }
    out[o++] = c;
  }
  out[o] = '\0';
}

static void collect_voice_mp3_headers(HTTPClient *http) {
  if (!http) {
    return;
  }
  http->collectHeaders(kVoiceMp3Headers, sizeof(kVoiceMp3Headers) / sizeof(kVoiceMp3Headers[0]));
}

static void voice_copy_mp3_headers(HTTPClient *http, PmVoiceResult *r) {
  if (!http || !r) {
    return;
  }
  const String transcript_raw = http->header("X-Voice-Transcript");
  if (transcript_raw.length() > 0) {
    copy_percent_decoded(transcript_raw, r->transcript, sizeof(r->transcript));
  }
  const String reply_raw = http->header("X-Voice-Reply");
  if (reply_raw.length() > 0) {
    copy_percent_decoded(reply_raw, r->reply, sizeof(r->reply));
  }
}

static void voice_print_reply_preview(const char *prefix, const char *reply) {
  if (!reply || reply[0] == '\0') {
    return;
  }
  char preview[181];
  size_t o = 0;
  for (const char *p = reply; *p && o + 1 < sizeof(preview); ++p) {
    preview[o++] = (*p == '\r' || *p == '\n') ? ' ' : *p;
  }
  preview[o] = '\0';
  Serial.printf("%s reply=\"%s%s\"\n", prefix ? prefix : "voice:", preview, reply[o] ? "…" : "");
}

static int voice_local_hour(void) {
  const time_t local = time(nullptr) + static_cast<time_t>(pm_geo_tz_offset_sec());
  struct tm tm = {};
  gmtime_r(&local, &tm);
  return tm.tm_hour;
}

static void voice_set_error(const char *msg) {
  if (!msg) {
    s_last_error[0] = '\0';
    return;
  }
  strncpy(s_last_error, msg, sizeof(s_last_error) - 1);
  s_last_error[sizeof(s_last_error) - 1] = '\0';
}

static char *voice_psram_char_alloc(size_t cap, const char *err) {
  char *p = static_cast<char *>(heap_caps_malloc(cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!p) {
    voice_set_error(err ? err : "oom psram");
    return nullptr;
  }
  p[0] = '\0';
  return p;
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
  r->audio_streamed = false;
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

static const char *json_find_last(const char *hay, const char *needle) {
  if (!hay || !needle) {
    return nullptr;
  }
  const char *last = nullptr;
  for (const char *p = strstr(hay, needle); p != nullptr; p = strstr(p + 1, needle)) {
    last = p;
  }
  return last;
}

/** Trim trailing chunked-encoding garbage after the final `}`. */
static size_t voice_json_trim_len(const char *buf, size_t len) {
  if (!buf || len < 2) {
    return len;
  }
  const char *route = json_find_last(buf, "\"route\":\"voice-pipeline\"}");
  if (route) {
    const char *end = strchr(route, '}');
    if (end && static_cast<size_t>(end - buf) + 1u <= len) {
      return static_cast<size_t>(end - buf) + 1u;
    }
  }
  const char *end = strrchr(buf, '}');
  if (end && static_cast<size_t>(end - buf) + 1u <= len) {
    return static_cast<size_t>(end - buf) + 1u;
  }
  return len;
}

/** True when the voice-pipeline JSON body looks complete (not truncated mid-field). */
static bool voice_response_json_complete(const char *buf, size_t len) {
  if (!buf || len < 8 || buf[0] != '{') {
    return false;
  }
  len = voice_json_trim_len(buf, len);
  const char *route = json_find_last(buf, "\"route\":\"voice-pipeline\"}");
  if (route) {
    const char *end = strchr(route, '}');
    return end != nullptr && static_cast<size_t>(end - buf) + 1u <= len;
  }
  const char *k = "\"audioBase64\":\"";
  const char *p = json_find_last(buf, k);
  if (p) {
    p += strlen(k);
    while (p < buf + len && *p != '"') {
      ++p;
    }
    return p < buf + len && *p == '"';
  }
  if (strstr(buf, "\"audioBase64\"") != nullptr) {
    return false;
  }
  size_t end = len;
  while (end > 0 && (buf[end - 1] == ' ' || buf[end - 1] == '\n' || buf[end - 1] == '\r' ||
                     buf[end - 1] == '\t' || buf[end - 1] == '0')) {
    --end;
  }
  return end >= 1 && buf[end - 1] == '}';
}

static bool voice_result_ok(PmVoiceResult *r) {
  if (!r) {
    return false;
  }
  if (r->mp3 && r->mp3_len >= 64) {
    return true;
  }
  if (r->audio_streamed || s_daily_briefing_streamed) {
    return true;
  }
  return r->reply[0] != '\0' || r->transcript[0] != '\0';
}

static bool extract_audio_base64(const char *json, uint8_t **out_bin, size_t *out_len) {
  const char *k = "\"audioBase64\":\"";
  const char *p = json_find_last(json, k);
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
  size_t cap = (b64len / 4) * 3 + 16;
  uint8_t *buf = static_cast<uint8_t *>(
      heap_caps_malloc(cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!buf) {
    buf = static_cast<uint8_t *>(malloc(cap));
  }
  if (!buf) {
    return false;
  }
  int rc = mbedtls_base64_decode(buf, cap, &olen, reinterpret_cast<const unsigned char *>(start), b64len);
  if (rc == MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL && cap < (b64len / 4) * 3 + 64) {
    free(buf);
    cap = (b64len / 4) * 3 + 64;
    buf = static_cast<uint8_t *>(heap_caps_malloc(cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!buf) {
      buf = static_cast<uint8_t *>(malloc(cap));
    }
    if (buf) {
      rc = mbedtls_base64_decode(buf, cap, &olen, reinterpret_cast<const unsigned char *>(start), b64len);
    }
  }
  if (rc != 0 || olen == 0) {
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
  const bool chunked = (declared < 0);
  const size_t target_len = (declared > 0) ? static_cast<size_t>(declared) : 0;
  const uint32_t deadline = millis() + 600000u;
  uint32_t last_rx_ms = 0;
  uint32_t last_prog_rd = 0;
  uint32_t last_stall_log_ms = 0;
  body_read_set_err("bad response");
  if (declared > 0) {
    Serial.printf("voice: Content-Length %d\n", declared);
  } else {
    Serial.println("voice: chunked response");
  }

  for (;;) {
    voice_task_wdt_reset();
    if (s_voice_cancel) {
      free(buf);
      body_read_set_err("cancelled");
      return false;
    }
    if (static_cast<int32_t>(millis() - deadline) >= 0) {
      ESP_LOGW(TAG, "HTTP body read timeout (%u bytes)", static_cast<unsigned>(rd));
      body_read_set_err("read timeout");
      break;
    }

    const int avail = stream->available();
    if (avail > 0) {
      if (rd >= read_cap) {
        body_read_set_err("reply too large");
        break;
      }
      const size_t take =
          static_cast<size_t>(avail) < (read_cap - rd) ? static_cast<size_t>(avail) : (read_cap - rd);
      const int n = stream->readBytes(buf + rd, take);
      if (n > 0) {
        rd += static_cast<size_t>(n);
        last_rx_ms = millis();
        buf[rd] = '\0';
        if (rd - last_prog_rd >= 32768u) {
          Serial.printf("voice: recv %u B…\n", static_cast<unsigned>(rd));
          last_prog_rd = rd;
        }
        if (target_len > 0 && rd >= target_len) {
          break;
        }
        if (chunked && rd >= 4096u && voice_response_json_complete(buf, rd)) {
          rd = voice_json_trim_len(buf, rd);
          buf[rd] = '\0';
          Serial.printf("voice: JSON complete at %u B\n", static_cast<unsigned>(rd));
          break;
        }
        continue;
      }
    }

    if (!http->connected() && stream->available() == 0) {
      break;
    }

    if (rd > 0 && last_rx_ms != 0) {
      const uint32_t idle = millis() - last_rx_ms;
      const uint32_t idle_limit = chunked ? (rd > 65536u ? 45000u : 15000u) : 12000u;
      if (idle > idle_limit) {
        Serial.printf("voice: HTTP idle %lu ms at %u B (chunked=%d)\n", static_cast<unsigned long>(idle),
                      static_cast<unsigned>(rd),
                      chunked ? 1 : 0);
        body_read_set_err("read stalled");
        break;
      }
      if (chunked && idle > 15000u && (millis() - last_stall_log_ms) > 15000u) {
        Serial.printf("voice: waiting… %u B (%lu ms idle)\n", static_cast<unsigned>(rd),
                      static_cast<unsigned long>(idle));
        last_stall_log_ms = millis();
      }
    }

    delay(5);
  }

  for (uint8_t drain = 0; drain < 48 && stream->available() > 0 && rd < read_cap; ++drain) {
    const int n = stream->readBytes(buf + rd, read_cap - rd);
    if (n > 0) {
      rd += static_cast<size_t>(n);
      last_rx_ms = millis();
    } else {
      delay(5);
    }
  }

  rd = voice_json_trim_len(buf, rd);
  buf[rd] = '\0';
  if (rd == 0) {
    ESP_LOGW(TAG, "HTTP body empty (declared %d)", declared);
    free(buf);
    body_read_set_err("bad response (empty)");
    return false;
  }

  if (rd >= read_cap) {
    ESP_LOGW(TAG, "HTTP body exceeds cap %u bytes", static_cast<unsigned>(read_cap));
    Serial.printf("pm_voice: reply too large (%u bytes)\n", static_cast<unsigned>(rd));
    free(buf);
    body_read_set_err("reply too large");
    return false;
  }

  if (target_len > 0 && rd < target_len) {
    ESP_LOGW(TAG, "HTTP short read %u/%u", static_cast<unsigned>(rd), static_cast<unsigned>(target_len));
    free(buf);
    body_read_set_err("bad response (truncated)");
    return false;
  }

  if (!voice_response_json_complete(buf, rd)) {
    const int extra = stream->available();
    ESP_LOGW(TAG, "HTTP incomplete JSON %u bytes (declared %d, +%d pending)", static_cast<unsigned>(rd), declared,
             extra);
    Serial.printf("pm_voice: incomplete JSON %u bytes declared=%d extra=%d\n", static_cast<unsigned>(rd),
                  declared, extra);
    if (rd > 160) {
      char tail[161];
      memcpy(tail, buf + rd - 160, 160);
      tail[160] = '\0';
      Serial.printf("pm_voice: tail …%s\n", tail);
    }
    free(buf);
    body_read_set_err("bad response (truncated)");
    return false;
  }

  *out_resp = buf;
  Serial.printf("voice: body complete %u B (declared %d)\n", static_cast<unsigned>(rd), declared);
  ESP_LOGI(TAG, "HTTP body %u bytes (declared %d)", static_cast<unsigned>(rd), declared);
  return true;
}

static bool read_http_mp3_body(HTTPClient *http, uint8_t **out_mp3, size_t *out_len) {
  if (!http || !out_mp3 || !out_len) {
    return false;
  }
  *out_mp3 = nullptr;
  *out_len = 0;

  const int declared = http->getSize();
  WiFiClient *stream = http->getStreamPtr();
  if (!stream) {
    return false;
  }
  if (declared <= 0) {
    body_read_set_err("missing mp3 length");
    return false;
  }
  if (declared > 768 * 1024) {
    ESP_LOGW(TAG, "MP3 Content-Length %d exceeds message cap", declared);
    body_read_set_err("reply too large");
    return false;
  }

  uint8_t *buf = static_cast<uint8_t *>(
      heap_caps_malloc(static_cast<size_t>(declared), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!buf) {
    body_read_set_err("oom mp3");
    return false;
  }

  size_t rd = 0;
  uint32_t last_rx_ms = millis();
  const uint32_t deadline = millis() + 180000u;
  body_read_set_err("bad response");
  Serial.printf("voice: MP3 Content-Length %d\n", declared);

  while (rd < static_cast<size_t>(declared)) {
    voice_task_wdt_reset();
    if (s_voice_cancel) {
      free(buf);
      body_read_set_err("cancelled");
      return false;
    }
    if (static_cast<int32_t>(millis() - deadline) >= 0) {
      free(buf);
      body_read_set_err("read timeout");
      return false;
    }

    const size_t remaining = static_cast<size_t>(declared) - rd;
    const size_t take = remaining > 4096u ? 4096u : remaining;
    const int n = stream->readBytes(buf + rd, take);
    if (n > 0) {
      rd += static_cast<size_t>(n);
      last_rx_ms = millis();
      if (rd == static_cast<size_t>(n) || rd / 65536u > (rd - static_cast<size_t>(n)) / 65536u) {
        Serial.printf("voice: MP3 recv %u B...\n", static_cast<unsigned>(rd));
      }
      continue;
    }

    if (!http->connected()) {
      break;
    }
    if (millis() - last_rx_ms > 25000u) {
      free(buf);
      body_read_set_err("read stalled");
      return false;
    }
    delay(5);
  }

  if (rd < 64 || rd < static_cast<size_t>(declared)) {
    free(buf);
    body_read_set_err(rd < 64 ? "bad response (empty)" : "bad response (truncated)");
    return false;
  }

  *out_mp3 = buf;
  *out_len = rd;
  Serial.printf("voice: MP3 complete %u B\n", static_cast<unsigned>(rd));
  return true;
}

static bool json_escape_string(const char *in, char *out, size_t out_cap) {
  if (!in || !out || out_cap < 4) {
    if (out && out_cap) {
      out[0] = '\0';
    }
    return false;
  }
  size_t j = 0;
  size_t i = 0;
  for (; in[i] != '\0' && j + 2 < out_cap; ++i) {
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
  return in[i] == '\0';
}

static void voice_set_http_error(int code) {
  if (code == 401) {
    voice_set_error("sign in on Castalia face");
  } else if (code == 422) {
    voice_set_error("no speech heard");
  } else {
    char errbuf[24];
    snprintf(errbuf, sizeof(errbuf), "HTTP %d", code);
    voice_set_error(errbuf);
  }
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
  r->audio_streamed = false;

  if (strlen(MYNAH_SUPABASE_URL) == 0 || strlen(MYNAH_SUPABASE_ANON_KEY) == 0) {
    voice_set_error("no supabase config");
    ESP_LOGW(TAG, "Supabase URL or anon key empty");
    return false;
  }
  if (!pm_voice_pipeline_host_ready(true)) {
    return false;
  }
  voice_prepare_mbedtls_psram();
  (void)pm_castalia_auth_prepare_for_voice();

  char base[160];
  strncpy(base, MYNAH_SUPABASE_URL, sizeof(base) - 1);
  base[sizeof(base) - 1] = '\0';
  trim_supabase_url(base, sizeof(base));

  char url[224];
  snprintf(url, sizeof(url), "%s/functions/v1/voice-pipeline", base);

  char *esc_msg = nullptr;
  char *esc_sys = nullptr;
  char *esc_face = nullptr;
  char *esc_faculty_slug = nullptr;
  char *esc_faculty_name = nullptr;

  const size_t msg_cap = strlen(message) * 2 + 16;
  esc_msg = voice_psram_char_alloc(msg_cap, "oom esc msg");
  if (!esc_msg || !json_escape_string(message, esc_msg, msg_cap)) {
    free(esc_msg);
    voice_set_error("message too long");
    return false;
  }

  if (system_instruction && system_instruction[0] != '\0') {
    const size_t sys_cap = strlen(system_instruction) * 2 + 16;
    esc_sys = voice_psram_char_alloc(sys_cap, "oom esc sys");
    if (!esc_sys || !json_escape_string(system_instruction, esc_sys, sys_cap)) {
      free(esc_msg);
      free(esc_face);
      free(esc_faculty_slug);
      free(esc_faculty_name);
      free(esc_sys);
      voice_set_error("system prompt too long");
      return false;
    }
  }

  if (s_req_face && s_req_face[0] != '\0') {
    const size_t face_cap = strlen(s_req_face) * 2 + 16;
    esc_face = voice_psram_char_alloc(face_cap, "oom esc face");
    if (!esc_face || !json_escape_string(s_req_face, esc_face, face_cap)) {
      free(esc_msg);
      free(esc_sys);
      free(esc_face);
      voice_set_error("face too long");
      return false;
    }
  }
  if (s_req_faculty_slug && s_req_faculty_slug[0] != '\0') {
    const size_t slug_cap = strlen(s_req_faculty_slug) * 2 + 16;
    esc_faculty_slug = voice_psram_char_alloc(slug_cap, "oom esc faculty");
    if (!esc_faculty_slug || !json_escape_string(s_req_faculty_slug, esc_faculty_slug, slug_cap)) {
      free(esc_msg);
      free(esc_sys);
      free(esc_face);
      free(esc_faculty_slug);
      voice_set_error("faculty slug too long");
      return false;
    }
  }
  if (s_req_faculty_name && s_req_faculty_name[0] != '\0') {
    const size_t name_cap = strlen(s_req_faculty_name) * 2 + 16;
    esc_faculty_name = voice_psram_char_alloc(name_cap, "oom esc faculty name");
    if (!esc_faculty_name || !json_escape_string(s_req_faculty_name, esc_faculty_name, name_cap)) {
      free(esc_msg);
      free(esc_sys);
      free(esc_face);
      free(esc_faculty_slug);
      free(esc_faculty_name);
      voice_set_error("faculty name too long");
      return false;
    }
  }

  const bool have_sys = esc_sys && esc_sys[0] != '\0';
  const bool have_face = esc_face && esc_face[0] != '\0';
  const bool have_faculty_slug = esc_faculty_slug && esc_faculty_slug[0] != '\0';
  const bool have_faculty_name = esc_faculty_name && esc_faculty_name[0] != '\0';
  const int local_hour = voice_local_hour();
  const size_t body_cap = strlen(esc_msg) + (have_sys ? strlen(esc_sys) : 0) +
                          (have_face ? strlen(esc_face) : 0) +
                          (have_faculty_slug ? strlen(esc_faculty_slug) : 0) +
                          (have_faculty_name ? strlen(esc_faculty_name) : 0) + 320;
  char *body = static_cast<char *>(
      heap_caps_malloc(body_cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!body) {
    free(esc_msg);
    free(esc_sys);
    free(esc_face);
    free(esc_faculty_slug);
    free(esc_faculty_name);
    voice_set_error("oom body");
    return false;
  }

  int n = snprintf(body, body_cap,
                   "{\"languageCode\":\"en-US\",\"message\":\"%s\"",
                   esc_msg);
  if (n > 0 && static_cast<size_t>(n) < body_cap && have_sys) {
    n += snprintf(body + n, body_cap - static_cast<size_t>(n), ",\"systemInstruction\":\"%s\"", esc_sys);
  }
  if (n > 0 && static_cast<size_t>(n) < body_cap && have_face) {
    n += snprintf(body + n, body_cap - static_cast<size_t>(n), ",\"face\":\"%s\"", esc_face);
  }
  if (n > 0 && static_cast<size_t>(n) < body_cap && have_faculty_slug) {
    n += snprintf(body + n, body_cap - static_cast<size_t>(n), ",\"facultySlug\":\"%s\"", esc_faculty_slug);
  }
  if (n > 0 && static_cast<size_t>(n) < body_cap && have_faculty_name) {
    n += snprintf(body + n, body_cap - static_cast<size_t>(n), ",\"facultyName\":\"%s\"", esc_faculty_name);
  }
  if (n > 0 && static_cast<size_t>(n) < body_cap) {
    n += snprintf(body + n, body_cap - static_cast<size_t>(n), ",\"responseFormat\":\"mp3\",\"localHour\":%d}", local_hour);
  }
  free(esc_msg);
  free(esc_sys);
  free(esc_face);
  free(esc_faculty_slug);
  free(esc_faculty_name);
  if (n <= 0 || static_cast<size_t>(n) >= body_cap) {
    free(body);
    voice_set_error("body too large");
    return false;
  }

  for (uint8_t attempt = 0; attempt < 2; ++attempt) {
    WiFiClientSecure client;
    HTTPClient http;
    voice_begin_http(&client, &http);
    if (!http.begin(client, url)) {
      http.end();
      if (attempt == 0) {
        Serial.println("voice: http begin retry");
        delay(900);
        continue;
      }
      free(body);
      voice_set_error("http begin");
      return false;
    }
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Accept", "audio/mpeg");
    collect_voice_mp3_headers(&http);
    pm_castalia_auth_apply_headers(&http);

    Serial.printf("voice: HTTP POST message (%u B body%s)…\n", static_cast<unsigned>(n),
                  attempt == 0 ? "" : ", retry");
    const int code = http.POST(reinterpret_cast<uint8_t *>(body), static_cast<size_t>(n));
    Serial.printf("voice: HTTP %d (message)\n", code);

    if (code == 200) {
      voice_copy_mp3_headers(&http, r);
      const bool ok = read_http_mp3_body(&http, &r->mp3, &r->mp3_len);
      http.end();
      free(body);
      if (!ok) {
        ESP_LOGW(TAG, "voice-pipeline (message) MP3 read fail");
        Serial.printf("voice: body read fail (%s)\n", s_body_read_err);
        voice_set_error(s_body_read_err);
        return false;
      }
      strncpy(r->transcript, message, sizeof(r->transcript) - 1);
      r->transcript[sizeof(r->transcript) - 1] = '\0';
      if (r->reply[0] == '\0') {
        strncpy(r->reply, "spoken MP3 response", sizeof(r->reply) - 1);
        r->reply[sizeof(r->reply) - 1] = '\0';
      }
      ESP_LOGI(TAG, "voice (message) mp3 %u bytes", static_cast<unsigned>(r->mp3_len));
      if (!voice_result_ok(r)) {
        voice_set_error("empty reply");
        return false;
      }
      voice_set_error(nullptr);
      return true;
    }

    ESP_LOGW(TAG, "voice-pipeline HTTP %d (message)", code);
    http.end();
    if (attempt == 0 && code < 0) {
      Serial.println("voice: HTTP retry after connect failure");
      delay(1300);
      continue;
    }
    free(body);
    voice_set_http_error(code);
    return false;
  }
  free(body);
  voice_set_error("http retry");
  return false;
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
  r->audio_streamed = false;

  if (strlen(MYNAH_SUPABASE_URL) == 0 || strlen(MYNAH_SUPABASE_ANON_KEY) == 0) {
    voice_set_error("no supabase config");
    ESP_LOGW(TAG, "Supabase URL or anon key empty");
    return false;
  }
  voice_prepare_mbedtls_psram();
  (void)pm_castalia_auth_prepare_for_voice();

  char base[160];
  strncpy(base, MYNAH_SUPABASE_URL, sizeof(base) - 1);
  base[sizeof(base) - 1] = '\0';
  trim_supabase_url(base, sizeof(base));

  char url[224];
  snprintf(url, sizeof(url), "%s/functions/v1/voice-pipeline", base);

  static const char kPrefix[] =
      "{\"languageCode\":\"en-US\",\"sampleRateHertz\":16000,\"audioBase64\":\"";
  const bool have_sys = system_instruction && system_instruction[0] != '\0';
  char *esc_sys_pcm = nullptr;
  if (have_sys) {
    const size_t esc_sys_cap = strlen(system_instruction) * 2 + 16;
    esc_sys_pcm = voice_psram_char_alloc(esc_sys_cap, "oom esc sys");
    if (!esc_sys_pcm || !json_escape_string(system_instruction, esc_sys_pcm, esc_sys_cap)) {
      free(esc_sys_pcm);
      voice_set_error("prompt too long");
      return false;
    }
  }

  const size_t b64max = ((pcm_len + 2) / 3) * 4 + 4;
  const size_t sys_extra = have_sys ? (24 + strlen(esc_sys_pcm)) : 0;
  const size_t body_cap = sizeof(kPrefix) - 1 + b64max + sys_extra + 4;
  uint8_t *body = static_cast<uint8_t *>(
      heap_caps_malloc(body_cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!body) {
    free(esc_sys_pcm);
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
    free(esc_sys_pcm);
    free(body);
    voice_set_error("b64 encode");
    return false;
  }
  size_t body_len = (sizeof(kPrefix) - 1) + nout;
  if (body_len + 2 > body_cap) {
    free(esc_sys_pcm);
    free(body);
    voice_set_error("body too large");
    return false;
  }
  body[body_len++] = '"';
  if (have_sys) {
    const int n = snprintf(reinterpret_cast<char *>(body + body_len), body_cap - body_len,
                           ",\"systemInstruction\":\"%s\"", esc_sys_pcm);
    if (n <= 0 || static_cast<size_t>(n) >= body_cap - body_len) {
      free(esc_sys_pcm);
      free(body);
      voice_set_error("body too large");
      return false;
    }
    body_len += static_cast<size_t>(n);
  }
  if (body_len + 1 > body_cap) {
    free(esc_sys_pcm);
    free(body);
    voice_set_error("body too large");
    return false;
  }
  body[body_len++] = '}';

  WiFiClientSecure client;
  HTTPClient http;
  voice_begin_http(&client, &http);
  if (!http.begin(client, url)) {
    free(esc_sys_pcm);
    free(body);
    voice_set_error("http begin");
    return false;
  }
  http.addHeader("Content-Type", "application/json");
  pm_castalia_auth_apply_headers(&http);

  Serial.printf("pm_voice: STT POST pcm=%u sys_esc=%u B\n", static_cast<unsigned>(pcm_len),
                static_cast<unsigned>(have_sys ? strlen(esc_sys_pcm) : 0));
  const int code = http.POST(body, body_len);
  free(esc_sys_pcm);
  free(body);

  if (code != 200) {
    ESP_LOGW(TAG, "voice-pipeline (pcm) HTTP %d", code);
    voice_set_http_error(code);
    http.end();
    return false;
  }

  const size_t resp_cap = kVoiceRespMaxBytes;
  const int declared_sz = http.getSize();
  Serial.printf("pm_voice: reading body (declared=%d)\n", declared_sz);
  char *resp = nullptr;
  if (!read_http_json_body(&http, &resp, resp_cap)) {
    ESP_LOGW(TAG, "voice-pipeline (pcm) body read fail (declared len %d)", declared_sz);
    voice_set_error(s_body_read_err);
    http.end();
    return false;
  }
  http.end();

  extract_json_string_field(resp, "transcript", r->transcript, sizeof(r->transcript));
  extract_json_string_field(resp, "reply", r->reply, sizeof(r->reply));
  if (r->transcript[0] != '\0') {
    Serial.printf("pm_voice: STT transcript: %.120s%s\n", r->transcript,
                  strlen(r->transcript) > 120 ? "…" : "");
  }
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

static bool voice_post_daily_briefing_inner(PmVoiceResult *r) {
  if (!r) {
    voice_set_error("no result");
    return false;
  }
  memset(r->transcript, 0, sizeof(r->transcript));
  memset(r->reply, 0, sizeof(r->reply));
  r->mp3 = nullptr;
  r->mp3_len = 0;
  r->audio_streamed = false;

  if (strlen(MYNAH_SUPABASE_URL) == 0 || strlen(MYNAH_SUPABASE_ANON_KEY) == 0) {
    voice_set_error("no supabase config");
    return false;
  }
  voice_prepare_mbedtls_psram();
  (void)pm_castalia_auth_prepare_for_voice();

  static constexpr size_t kFactsCap = 8192;
  char *facts = static_cast<char *>(heap_caps_malloc(kFactsCap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!facts) {
    facts = static_cast<char *>(malloc(kFactsCap));
  }
  if (!facts) {
    voice_set_error("oom facts");
    return false;
  }
  if (!pm_daily_briefing_build_device_facts(facts, kFactsCap)) {
    facts[0] = '\0';
  }

  char *esc_facts = nullptr;
  const size_t esc_cap = strlen(facts) * 2 + 16;
  esc_facts = voice_psram_char_alloc(esc_cap, "oom esc facts");
  if (!esc_facts || !json_escape_string(facts, esc_facts, esc_cap)) {
    free(facts);
    free(esc_facts);
    voice_set_error("facts too long");
    return false;
  }
  free(facts);

  const time_t epoch = time(nullptr);
  const int local_hour = voice_local_hour();
  const size_t body_cap = strlen(esc_facts) + 224;
  char *body = static_cast<char *>(heap_caps_malloc(body_cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!body) {
    free(esc_facts);
    voice_set_error("oom body");
    return false;
  }

  int n;
  if (esc_facts[0] != '\0') {
    n = snprintf(body, body_cap,
                 "{\"face\":\"daily_briefing\",\"epochSeconds\":%lld,"
                 "\"briefingFacts\":\"%s\",\"responseFormat\":\"mp3\",\"localHour\":%d}",
                 static_cast<long long>(epoch), esc_facts, local_hour);
  } else {
    n = snprintf(body, body_cap,
                 "{\"face\":\"daily_briefing\",\"epochSeconds\":%lld,\"responseFormat\":\"mp3\","
                 "\"localHour\":%d}",
                 static_cast<long long>(epoch), local_hour);
  }
  free(esc_facts);
  if (n <= 0 || static_cast<size_t>(n) >= body_cap) {
    free(body);
    voice_set_error("body too large");
    return false;
  }

  char base[160];
  strncpy(base, MYNAH_SUPABASE_URL, sizeof(base) - 1);
  base[sizeof(base) - 1] = '\0';
  trim_supabase_url(base, sizeof(base));
  char url[224];
  snprintf(url, sizeof(url), "%s/functions/v1/voice-pipeline", base);

  WiFiClientSecure client;
  HTTPClient http;
  voice_begin_http(&client, &http);
  if (!http.begin(client, url)) {
    free(body);
    voice_set_error("http begin");
    return false;
  }
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Accept", "audio/mpeg");
  collect_voice_mp3_headers(&http);
  pm_castalia_auth_apply_headers(&http);

  Serial.printf("voice: POST daily_briefing (%u B)…\n", static_cast<unsigned>(n));
  const int code = http.POST(reinterpret_cast<uint8_t *>(body), static_cast<size_t>(n));
  free(body);
  Serial.printf("voice: HTTP %d (daily_briefing)\n", code);

  if (code != 200) {
    WiFiClient *stream = http.getStreamPtr();
    if (stream && stream->available()) {
      char errsnippet[160] = "";
      const size_t n = stream->readBytes(errsnippet, sizeof(errsnippet) - 1);
      errsnippet[n] = '\0';
      Serial.printf("voice: daily_briefing err: %s\n", errsnippet);
    }
    voice_set_http_error(code);
    http.end();
    return false;
  }

  const String ctype = http.header("Content-Type");
  if (ctype.indexOf("audio/mpeg") < 0 && ctype.indexOf("audio/mp3") < 0) {
    ESP_LOGW(TAG, "daily_briefing unexpected Content-Type: %s", ctype.c_str());
  }
  voice_copy_mp3_headers(&http, r);

  WiFiClient *stream = http.getStreamPtr();
  const int declared = http.getSize();
  s_daily_briefing_streamed = false;
  s_daily_briefing_streaming_play = false;
  if (!stream) {
    voice_set_error("no stream");
    http.end();
    return false;
  }
  s_daily_briefing_streaming_play = true;
  if (!pm_speaker_play_mp3_http_stream(stream, declared, &s_voice_cancel)) {
    s_daily_briefing_streaming_play = false;
    voice_set_error(s_voice_cancel ? "cancelled" : "stream playback failed");
    http.end();
    return false;
  }
  http.end();
  s_daily_briefing_streaming_play = false;
  s_daily_briefing_streamed = true;
  r->audio_streamed = true;

  strncpy(r->transcript, "daily briefing", sizeof(r->transcript) - 1);

  if (!voice_result_ok(r)) {
    voice_set_error("empty briefing audio");
    return false;
  }
  voice_set_error(nullptr);
  ESP_LOGI(TAG, "daily briefing streamed");
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
  r->audio_streamed = false;

  if (strlen(MYNAH_SUPABASE_URL) == 0 || strlen(MYNAH_SUPABASE_ANON_KEY) == 0) {
    voice_set_error("no supabase config");
    return false;
  }
  voice_prepare_mbedtls_psram();
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
  HTTPClient http;
  voice_begin_http(&client, &http);
  if (!http.begin(client, url)) {
    voice_set_error("http begin");
    return false;
  }
  http.addHeader("Content-Type", "application/json");
  pm_castalia_auth_apply_headers(&http);

  const int code = http.POST(reinterpret_cast<uint8_t *>(body), strlen(body));
  if (code != 200) {
    ESP_LOGW(TAG, "voice-pipeline (clock_agenda) HTTP %d", code);
    voice_set_http_error(code);
    http.end();
    return false;
  }

  const size_t resp_cap = kVoiceRespMaxBytes;
  char *resp = nullptr;
  if (!read_http_json_body(&http, &resp, resp_cap)) {
    voice_set_error(s_body_read_err);
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
    s_voice_task_active = true;
    const uint8_t op = s_voice_op;
    if (op == 1) {
      Serial.println("voice: POST message…");
      s_voice_ok = voice_post_message_inner(s_req_message, s_req_system, s_req_result);
      if (s_voice_ok && s_req_result && s_req_result->mp3_len > 0) {
        Serial.printf("voice: message ok mp3=%u B\n", static_cast<unsigned>(s_req_result->mp3_len));
        voice_print_reply_preview("voice: message", s_req_result->reply);
      } else {
        Serial.printf("voice: message %s (%s)\n", s_voice_ok ? "ok" : "fail",
                      s_last_error[0] ? s_last_error : "-");
      }
    } else if (op == 2) {
      s_voice_ok = voice_post_pcm_inner(s_req_pcm, s_req_pcm_len, s_req_system, s_req_result);
    } else if (op == 3) {
      s_voice_ok = voice_post_clock_agenda_inner(s_req_result);
    } else if (op == 4) {
      Serial.println("voice: POST daily_briefing…");
      s_voice_ok = voice_post_daily_briefing_inner(s_req_result);
      if (s_voice_ok && s_req_result && s_req_result->mp3_len > 0) {
        Serial.printf("voice: daily_briefing ok mp3=%u B\n",
                      static_cast<unsigned>(s_req_result->mp3_len));
        voice_print_reply_preview("voice: daily_briefing", s_req_result->reply);
      } else {
        Serial.printf("voice: daily_briefing %s (%s)\n", s_voice_ok ? "ok" : "fail",
                      s_last_error[0] ? s_last_error : "-");
      }
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
    s_voice_task_active = false;
  }
}

static void voice_net_task_ensure() {
  if (s_voice_task) {
    return;
  }
  BaseType_t ok = xTaskCreatePinnedToCoreWithCaps(voice_net_task, "voice_net", kVoiceNetTaskStack, nullptr, 1,
                                                  &s_voice_task, 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  s_voice_task_with_caps = ok == pdPASS;
  if (ok != pdPASS) {
    ok = xTaskCreatePinnedToCore(voice_net_task, "voice_net", kVoiceNetTaskStack, nullptr, 1,
                                 &s_voice_task, 1);
    s_voice_task_with_caps = false;
  }
  if (ok != pdPASS) {
    s_voice_task = nullptr;
    voice_set_error("voice task create failed");
    Serial.printf("voice: task create failed stack=%u internal=%u largest=%u psram=%u\n",
                  static_cast<unsigned>(kVoiceNetTaskStack),
                  static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                  static_cast<unsigned>(
                      heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                  static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)));
  }
}

static bool voice_net_begin(uint8_t op) {
  pm_speaker_release_idle_task();
  voice_net_task_ensure();
  if (!s_voice_task) {
    return false;
  }
  if (s_voice_status == PmVoiceStatus::Working || s_voice_task_active) {
    ESP_LOGW(TAG, "voice_begin while busy");
    voice_set_error(s_voice_task_active ? "voice busy" : "busy");
    return false;
  }
  s_voice_cancel = false;
  s_daily_briefing_streamed = false;
  s_daily_briefing_streaming_play = false;
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
  return pm_voice_begin_message_ex(message, system_instruction, nullptr, nullptr, nullptr, r);
}

bool pm_voice_begin_message_ex(const char *message, const char *system_instruction, const char *face,
                               const char *faculty_slug, const char *faculty_name, PmVoiceResult *r) {
  s_req_message = message;
  s_req_system = system_instruction;
  s_req_face = face;
  s_req_faculty_slug = faculty_slug;
  s_req_faculty_name = faculty_name;
  s_req_result = r;
  return voice_net_begin(1);
}

bool pm_voice_begin_pcm(const uint8_t *pcm, size_t pcm_len, const char *system_instruction, PmVoiceResult *r) {
  s_req_pcm = pcm;
  s_req_pcm_len = pcm_len;
  s_req_system = system_instruction;
  s_req_face = nullptr;
  s_req_faculty_slug = nullptr;
  s_req_faculty_name = nullptr;
  s_req_result = r;
  return voice_net_begin(2);
}

bool pm_voice_begin_clock_agenda(PmVoiceResult *r) {
  s_req_face = nullptr;
  s_req_faculty_slug = nullptr;
  s_req_faculty_name = nullptr;
  s_req_result = r;
  return voice_net_begin(3);
}

bool pm_voice_begin_daily_briefing(PmVoiceResult *r) {
  s_req_face = nullptr;
  s_req_faculty_slug = nullptr;
  s_req_faculty_name = nullptr;
  s_req_result = r;
  return voice_net_begin(4);
}

PmVoiceStatus pm_voice_poll(void) {
  return s_voice_status;
}

bool pm_voice_release_idle_task(void) {
  if (!s_voice_task || s_voice_task_active || s_voice_status == PmVoiceStatus::Working) {
    return false;
  }
  TaskHandle_t task = s_voice_task;
  const bool task_with_caps = s_voice_task_with_caps;
  s_voice_task = nullptr;
  s_voice_task_with_caps = false;
  if (task_with_caps) {
    vTaskDeleteWithCaps(task);
  } else {
    vTaskDelete(task);
  }
  return true;
}

bool pm_voice_daily_briefing_streamed(void) {
  return s_daily_briefing_streamed;
}

bool pm_voice_daily_briefing_streaming_play(void) {
  return s_daily_briefing_streaming_play;
}

void pm_voice_abort(void) {
  s_voice_cancel = true;
  if (s_last_error[0] == '\0') {
    voice_set_error("cancelled");
  }
  s_voice_status = PmVoiceStatus::DoneFail;
  s_voice_done = true;
}

bool pm_voice_post_message(const char *message, const char *system_instruction, PmVoiceResult *r) {
  return pm_voice_post_message_ex(message, system_instruction, nullptr, nullptr, nullptr, r);
}

bool pm_voice_post_message_ex(const char *message, const char *system_instruction, const char *face,
                              const char *faculty_slug, const char *faculty_name, PmVoiceResult *r) {
  s_req_message = message;
  s_req_system = system_instruction;
  s_req_face = face;
  s_req_faculty_slug = faculty_slug;
  s_req_faculty_name = faculty_name;
  s_req_result = r;
  return voice_net_run(1, kVoiceHttpTimeoutMs + 5000u);
}

uint32_t pm_voice_stack_high_water(void) {
  return s_voice_task ? static_cast<uint32_t>(uxTaskGetStackHighWaterMark(s_voice_task)) : 0u;
}

bool pm_voice_post_pcm(const uint8_t *pcm, size_t pcm_len, const char *system_instruction, PmVoiceResult *r) {
  s_req_pcm = pcm;
  s_req_pcm_len = pcm_len;
  s_req_system = system_instruction;
  s_req_face = nullptr;
  s_req_faculty_slug = nullptr;
  s_req_faculty_name = nullptr;
  s_req_result = r;
  return voice_net_run(2, kVoiceHttpTimeoutMs + 5000u);
}
