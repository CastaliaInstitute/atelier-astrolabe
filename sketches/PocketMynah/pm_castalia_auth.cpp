#include "pm_castalia_auth.h"

#include <HTTPClient.h>
#include <JPEGDEC.h>
#include <Preferences.h>
#include <WiFiClientSecure.h>
#include <cstring>
#include <ctime>

#include "Arduino_GFX_Library.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pm_config.h"
#include "pm_wifi_ntp.h"

extern "C" {
#include "third_party/qrcodegen/qrcodegen.h"
}

static const char *TAG = "pm_castalia";
static const char kNs[] = "castalia";
static const char kAt[] = "access_token";
static const char kRt[] = "refresh_token";
static const char kEx[] = "exp_ms";

static Preferences s_pref;
static bool s_inited = false;

static char s_pair_id[48] = "";
static char s_pair_secret[96] = "";
static char s_signin_url[512] = "";
static char s_status[120] = "";

static char s_access[1536] = "";
static char s_refresh[800] = "";
static uint64_t s_expires_at_ms = 0;

/** Cap QR version (long sign-in URLs fit v12; v40 buffers blow stack/BSS and slow draw). */
static constexpr int kCastaliaQrMaxVersion = 12;
static constexpr size_t kCastaliaQrBufLen = qrcodegen_BUFFER_LEN_FOR_VERSION(kCastaliaQrMaxVersion);

static uint8_t s_qrcodegen_temp[kCastaliaQrBufLen];
static uint8_t s_qrcodegen_out[kCastaliaQrBufLen];
static char s_http_json_buf[4096];
static bool s_pair_start_pending = false;
static bool s_warmup_requested = false;
static uint32_t s_last_poll_ms = 0;
static uint32_t s_poll_quiet_until_ms = 0;
static int s_qr_cached_size = 0;
static int s_qr_cached_mod = 0;

static constexpr uint32_t kCastaliaPollQuietAfterQrMs = 15000;
static constexpr uint32_t kCastaliaNetTaskStack = 32768;

static TaskHandle_t s_net_task = nullptr;
static volatile bool s_net_done = false;
static volatile bool s_net_ok = false;
static volatile uint8_t s_net_op = 0; /** 1 = pair start, 2 = poll, 3 = refresh session, 4 = profile */

static char s_profile_name[64] = "";
static char s_profile_initials[4] = "";
static char s_profile_avatar_url[384] = "";
static bool s_profile_fetch_pending = false;
static bool s_profile_fetching = false;
static bool s_profile_name_ready = false;
static bool s_profile_avatar_ready = false;
static uint16_t *s_profile_avatar_fb = nullptr;
static int s_profile_avatar_w = 0;
static int s_profile_avatar_h = 0;

static char s_poll_enc_id[80];
static char s_poll_enc_sec[160];
static char s_poll_url[400];
static char s_poll_base[160];
/** Skip qrcodegen_encodeText when sign-in URL unchanged (full_paint runs every second). */
static char s_qr_cached_url[sizeof(s_signin_url)] = "";
static bool s_qr_modules_valid = false;

static constexpr uint32_t kCastaliaHttpTimeoutMs = 12000;
static constexpr uint32_t kCastaliaBodyReadMs = 10000;
static constexpr uint32_t kCastaliaPollIntervalMs = 2000;

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

static void profile_compute_initials(const char *name) {
  s_profile_initials[0] = '\0';
  if (!name || !name[0]) {
    return;
  }
  char a = 0;
  char b = 0;
  bool in_word = false;
  for (const char *p = name; *p && !b; ++p) {
    const unsigned char c = static_cast<unsigned char>(*p);
    const bool alnum =
        (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
    if (alnum) {
      if (!in_word) {
        in_word = true;
        const char up = (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : static_cast<char>(c);
        if (!a) {
          a = up;
        } else if (!b) {
          b = up;
        }
      }
    } else {
      in_word = false;
    }
  }
  if (!a) {
    return;
  }
  if (!b) {
    b = a;
  }
  s_profile_initials[0] = a;
  s_profile_initials[1] = b;
  s_profile_initials[2] = '\0';
}

static bool profile_pick_name_from_user_json(const char *json, char *out, size_t out_cap) {
  if (!json || !out || out_cap < 2) {
    return false;
  }
  if (extract_json_string_field(json, "full_name", out, out_cap) && out[0]) {
    return true;
  }
  if (extract_json_string_field(json, "name", out, out_cap) && out[0]) {
    return true;
  }
  if (extract_json_string_field(json, "email", out, out_cap) && out[0]) {
    char *at = strchr(out, '@');
    if (at) {
      *at = '\0';
    }
    return out[0] != '\0';
  }
  return false;
}

static bool profile_pick_avatar_url(const char *json, char *out, size_t out_cap) {
  if (!json || !out || out_cap < 8) {
    return false;
  }
  if (extract_json_string_field(json, "avatar_url", out, out_cap) && out[0]) {
    return true;
  }
  if (extract_json_string_field(json, "picture", out, out_cap) && out[0]) {
    return true;
  }
  return false;
}

static void profile_free_avatar_fb() {
  if (s_profile_avatar_fb) {
    free(s_profile_avatar_fb);
    s_profile_avatar_fb = nullptr;
  }
  s_profile_avatar_w = 0;
  s_profile_avatar_h = 0;
  s_profile_avatar_ready = false;
}

void pm_castalia_profile_clear() {
  s_profile_name[0] = '\0';
  s_profile_initials[0] = '\0';
  s_profile_avatar_url[0] = '\0';
  s_profile_name_ready = false;
  s_profile_fetch_pending = false;
  s_profile_fetching = false;
  profile_free_avatar_fb();
}

static void profile_request_fetch() {
  if (!pm_castalia_has_session()) {
    return;
  }
  s_profile_fetch_pending = true;
}

static bool extract_json_long_field(const char *json, const char *key, long *out) {
  char pat[40];
  snprintf(pat, sizeof(pat), "\"%s\":", key);
  const char *p = strstr(json, pat);
  if (!p) {
    return false;
  }
  p += strlen(pat);
  while (*p == ' ' || *p == '\t') {
    ++p;
  }
  *out = strtol(p, nullptr, 10);
  return true;
}

static void url_encode_component(const char *in, char *out, size_t cap) {
  static const char *hex = "0123456789ABCDEF";
  size_t j = 0;
  for (size_t i = 0; in[i] && j + 4 < cap; ++i) {
    const unsigned char c = static_cast<unsigned char>(in[i]);
    const bool safe =
        (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~';
    if (safe) {
      out[j++] = static_cast<char>(c);
    } else {
      out[j++] = '%';
      out[j++] = hex[(c >> 4) & 15];
      out[j++] = hex[c & 15];
    }
  }
  out[j] = '\0';
}

static bool read_small_json_body(HTTPClient *http, char *buf, size_t cap) {
  buf[0] = '\0';
  WiFiClient *stream = http->getStreamPtr();
  if (!stream) {
    return false;
  }
  size_t rd = 0;
  const uint32_t deadline = millis() + kCastaliaBodyReadMs;
  while (rd + 1 < cap) {
    const int avail = stream->available();
    if (avail > 0) {
      const int n = stream->readBytes(buf + rd, static_cast<size_t>(avail) < (cap - 1 - rd) ? avail : static_cast<int>(cap - 1 - rd));
      if (n > 0) {
        rd += static_cast<size_t>(n);
        continue;
      }
    }
    if (!http->connected() && stream->available() == 0) {
      break;
    }
    if (static_cast<int32_t>(millis() - deadline) >= 0) {
      break;
    }
    yield();
    delay(1);
  }
  buf[rd] = '\0';
  return rd > 0;
}

static void prefs_load() {
  if (!s_pref.begin(kNs, true)) {
    return;
  }
  s_access[0] = '\0';
  s_refresh[0] = '\0';
  s_expires_at_ms = 0;
  if (s_pref.isKey(kAt)) {
    s_pref.getString(kAt, s_access, sizeof(s_access));
  }
  if (s_pref.isKey(kRt)) {
    s_pref.getString(kRt, s_refresh, sizeof(s_refresh));
  }
  if (s_pref.isKey(kEx)) {
    const String exp_str = s_pref.getString(kEx, "0");
    s_expires_at_ms = strtoull(exp_str.c_str(), nullptr, 10);
  }
  s_pref.end();
}

static void prefs_save_session() {
  if (!s_pref.begin(kNs, false)) {
    return;
  }
  s_pref.putString(kAt, s_access);
  s_pref.putString(kRt, s_refresh);
  char expbuf[24];
  snprintf(expbuf, sizeof(expbuf), "%llu", static_cast<unsigned long long>(s_expires_at_ms));
  s_pref.putString(kEx, expbuf);
  s_pref.end();
}

static void prefs_clear_session() {
  if (!s_pref.begin(kNs, false)) {
    return;
  }
  s_pref.remove(kAt);
  s_pref.remove(kRt);
  s_pref.remove(kEx);
  s_pref.end();
  s_access[0] = '\0';
  s_refresh[0] = '\0';
  s_expires_at_ms = 0;
  pm_castalia_profile_clear();
}

static bool wall_time_ok() {
  return time(nullptr) > 100000;
}

static uint64_t auth_now_ms() {
  if (wall_time_ok()) {
    return static_cast<uint64_t>(time(nullptr)) * 1000ULL;
  }
  return static_cast<uint64_t>(millis());
}

/** True when access token is past expiry. */
static bool access_token_dead() {
  if (s_access[0] == '\0' || s_expires_at_ms == 0) {
    return true;
  }
  return auth_now_ms() >= s_expires_at_ms;
}

/** True when access token should be refreshed (before hard expiry). */
static bool access_token_stale() {
  if (s_access[0] == '\0' || s_expires_at_ms == 0) {
    return true;
  }
  return auth_now_ms() + 120000ULL >= s_expires_at_ms;
}

static bool refresh_session_http() {
  if (s_refresh[0] == '\0' || strlen(MYNAH_SUPABASE_URL) == 0) {
    return false;
  }
  char base[160];
  strncpy(base, MYNAH_SUPABASE_URL, sizeof(base) - 1);
  base[sizeof(base) - 1] = '\0';
  trim_supabase_url(base, sizeof(base));
  char url[200];
  snprintf(url, sizeof(url), "%s/auth/v1/token?grant_type=refresh_token", base);

  char body[900];
  snprintf(body, sizeof(body), "{\"refresh_token\":\"%s\"}", s_refresh);
  // refresh_token may contain quotes? unlikely - if so would need JSON escape; JWT uses . -

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(kCastaliaHttpTimeoutMs);
  if (!http.begin(client, url)) {
    return false;
  }
  http.addHeader("Content-Type", "application/json");
  http.addHeader("apikey", MYNAH_SUPABASE_ANON_KEY);
  http.addHeader("Authorization", String("Bearer ") + MYNAH_SUPABASE_ANON_KEY);
  const int code = http.POST(body);
  if (code != 200) {
    ESP_LOGW(TAG, "refresh HTTP %d", code);
    http.end();
    prefs_clear_session();
    return false;
  }
  if (!read_small_json_body(&http, s_http_json_buf, sizeof(s_http_json_buf))) {
    http.end();
    return false;
  }
  http.end();

  char at[sizeof(s_access)] = "";
  char rt[sizeof(s_refresh)] = "";
  long exp_in = 3600;
  if (!extract_json_string_field(s_http_json_buf, "access_token", at, sizeof(at)) ||
      !extract_json_string_field(s_http_json_buf, "refresh_token", rt, sizeof(rt))) {
    prefs_clear_session();
    return false;
  }
  extract_json_long_field(s_http_json_buf, "expires_in", &exp_in);
  if (exp_in < 60) {
    exp_in = 3600;
  }
  strncpy(s_access, at, sizeof(s_access) - 1);
  s_access[sizeof(s_access) - 1] = '\0';
  strncpy(s_refresh, rt, sizeof(s_refresh) - 1);
  s_refresh[sizeof(s_refresh) - 1] = '\0';
  const time_t now_sec = time(nullptr);
  if (now_sec > 100000) {
    s_expires_at_ms = static_cast<uint64_t>(now_sec) * 1000ULL + static_cast<uint64_t>(exp_in) * 1000ULL;
  }
  prefs_save_session();
  return true;
}

void pm_castalia_auth_init() {
  if (s_inited) {
    return;
  }
  s_inited = true;
  prefs_load();
}

bool pm_castalia_has_session() {
  pm_castalia_auth_init();
  if (s_access[0] == '\0' || s_refresh[0] == '\0') {
    return false;
  }
  if (!wall_time_ok()) {
    return true;
  }
  return !access_token_dead();
}

void pm_castalia_auth_bearer(char *out, size_t out_cap) {
  pm_castalia_auth_init();
  if (!out || out_cap < 8) {
    return;
  }
  out[0] = '\0';
  if (strlen(MYNAH_SUPABASE_ANON_KEY) == 0) {
    return;
  }
  if (s_access[0] != '\0' && !access_token_dead()) {
    strncpy(out, s_access, out_cap - 1);
    out[out_cap - 1] = '\0';
    return;
  }
  strncpy(out, MYNAH_SUPABASE_ANON_KEY, out_cap - 1);
  out[out_cap - 1] = '\0';
}

bool pm_castalia_auth_prepare_for_voice() {
  pm_castalia_auth_init();
  if (s_refresh[0] != '\0' && (access_token_stale() || access_token_dead())) {
    if (!refresh_session_http()) {
      ESP_LOGW(TAG, "session refresh failed; trying anon for voice");
      s_access[0] = '\0';
    }
  }
  return true;
}

static char s_auth_bearer_buf[1536];

void pm_castalia_auth_apply_headers(HTTPClient *http) {
  if (!http) {
    return;
  }
  pm_castalia_auth_bearer(s_auth_bearer_buf, sizeof(s_auth_bearer_buf));
  http->addHeader("Authorization", String("Bearer ") + s_auth_bearer_buf);
  http->addHeader("apikey", MYNAH_SUPABASE_ANON_KEY);
}

static void invalidate_qr_cache() {
  s_qr_cached_url[0] = '\0';
  s_qr_modules_valid = false;
  s_qr_cached_size = 0;
  s_qr_cached_mod = 0;
}

static bool pm_castalia_encode_qr_cache() {
  if (s_signin_url[0] == '\0') {
    return false;
  }
  if (s_qr_modules_valid && strcmp(s_signin_url, s_qr_cached_url) == 0 && s_qr_cached_size > 0) {
    return true;
  }
  if (!qrcodegen_encodeText(s_signin_url, s_qrcodegen_temp, s_qrcodegen_out, qrcodegen_Ecc_LOW, qrcodegen_VERSION_MIN,
                           kCastaliaQrMaxVersion, qrcodegen_Mask_AUTO, true)) {
    ESP_LOGW(TAG, "QR encode failed (url len %u)", static_cast<unsigned>(strlen(s_signin_url)));
    invalidate_qr_cache();
    return false;
  }
  strncpy(s_qr_cached_url, s_signin_url, sizeof(s_qr_cached_url) - 1);
  s_qr_cached_url[sizeof(s_qr_cached_url) - 1] = '\0';
  s_qr_modules_valid = true;
  s_qr_cached_size = qrcodegen_getSize(s_qrcodegen_out);
  return s_qr_cached_size > 0;
}

static bool castalia_pair_start_http();
static bool pm_castalia_poll_pairing();
static bool castalia_fetch_profile_http();

static void castalia_net_task(void *arg) {
  (void)arg;
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    const uint8_t op = s_net_op;
    if (op == 1) {
      s_net_ok = castalia_pair_start_http();
    } else if (op == 2) {
      s_net_ok = pm_castalia_poll_pairing();
    } else if (op == 3) {
      pm_castalia_auth_init();
      s_net_ok = refresh_session_http();
    } else if (op == 4) {
      pm_castalia_auth_init();
      s_net_ok = castalia_fetch_profile_http();
    } else {
      s_net_ok = false;
    }
    s_net_done = true;
  }
}

static void castalia_net_task_ensure() {
  if (s_net_task) {
    return;
  }
  xTaskCreatePinnedToCore(castalia_net_task, "castalia_net", kCastaliaNetTaskStack, nullptr, 1, &s_net_task, 1);
}

static bool castalia_net_run(uint8_t op, uint32_t timeout_ms) {
  castalia_net_task_ensure();
  if (!s_net_task) {
    return false;
  }
  s_net_op = op;
  s_net_done = false;
  xTaskNotify(s_net_task, 1, eSetBits);
  const uint32_t deadline = millis() + timeout_ms;
  while (!s_net_done) {
    delay(10);
    if (static_cast<int32_t>(millis() - deadline) >= 0) {
      ESP_LOGW(TAG, "net op %u timeout", static_cast<unsigned>(op));
      return false;
    }
  }
  return s_net_ok;
}

bool pm_castalia_tick_refresh_session() {
  pm_castalia_auth_init();
  if (!pm_wifi_connected() || s_refresh[0] == '\0') {
    return false;
  }
  if (!access_token_stale() && !access_token_dead()) {
    return false;
  }
  return castalia_net_run(3, kCastaliaHttpTimeoutMs + 4000u);
}

static void build_signin_url() {
  s_signin_url[0] = '\0';
  invalidate_qr_cache();
  if (s_pair_id[0] == '\0' || s_pair_secret[0] == '\0') {
    return;
  }
  char enc_key[200];
  url_encode_component(s_pair_secret, enc_key, sizeof(enc_key));
  char path[280];
  snprintf(path, sizeof(path), "/auth/mynah-device/?pair=%s&key=%s", s_pair_id, enc_key);
  char enc_path[400];
  url_encode_component(path, enc_path, sizeof(enc_path));

  const char *origin = MYNAH_CASTALIA_WEB_ORIGIN;
  char origin_trim[96];
  strncpy(origin_trim, origin, sizeof(origin_trim) - 1);
  origin_trim[sizeof(origin_trim) - 1] = '\0';
  trim_supabase_url(origin_trim, sizeof(origin_trim));
  /** Real HTML on castalia.institute (Supabase Edge serves HTML as text/plain). */
  snprintf(s_signin_url, sizeof(s_signin_url), "%s/auth/signin/?provider=google&redirect=%s", origin_trim, enc_path);
}

static bool http_post_json(const char *url, const char *body, char *resp, size_t resp_cap, int *http_code_out) {
  if (http_code_out) {
    *http_code_out = -1;
  }
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(kCastaliaHttpTimeoutMs);
  if (!http.begin(client, url)) {
    return false;
  }
  http.addHeader("Content-Type", "application/json");
  http.addHeader("apikey", MYNAH_SUPABASE_ANON_KEY);
  http.addHeader("Authorization", String("Bearer ") + MYNAH_SUPABASE_ANON_KEY);
  const int code = http.POST(body ? body : "{}");
  if (http_code_out) {
    *http_code_out = code;
  }
  if (code != 200) {
    ESP_LOGW(TAG, "POST %s -> %d", url, code);
    read_small_json_body(&http, resp, resp_cap);
    http.end();
    return false;
  }
  const bool ok = read_small_json_body(&http, resp, resp_cap);
  http.end();
  return ok;
}

static bool http_get_text(const char *url, char *resp, size_t resp_cap) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(kCastaliaHttpTimeoutMs);
  if (!http.begin(client, url)) {
    return false;
  }
  http.addHeader("apikey", MYNAH_SUPABASE_ANON_KEY);
  http.addHeader("Authorization", String("Bearer ") + MYNAH_SUPABASE_ANON_KEY);
  const int code = http.GET();
  if (code != 200) {
    ESP_LOGW(TAG, "GET %s -> %d", url, code);
    read_small_json_body(&http, resp, resp_cap);
    http.end();
    return false;
  }
  const bool ok = read_small_json_body(&http, resp, resp_cap);
  http.end();
  return ok;
}

static bool http_get_authed(const char *url, char *resp, size_t resp_cap) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(kCastaliaHttpTimeoutMs);
  if (!http.begin(client, url)) {
    return false;
  }
  pm_castalia_auth_apply_headers(&http);
  const int code = http.GET();
  if (code != 200) {
    ESP_LOGW(TAG, "GET authed %s -> %d", url, code);
    http.end();
    return false;
  }
  const bool ok = read_small_json_body(&http, resp, resp_cap);
  http.end();
  return ok;
}

static bool http_download_binary(const char *url, uint8_t **out_buf, size_t *out_len) {
  if (!out_buf || !out_len) {
    return false;
  }
  *out_buf = nullptr;
  *out_len = 0;
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(kCastaliaHttpTimeoutMs);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!http.begin(client, url)) {
    return false;
  }
  const int code = http.GET();
  const int len = http.getSize();
  if (code != 200 || len <= 0 || len > 65536) {
    ESP_LOGW(TAG, "avatar GET %d len %d", code, len);
    http.end();
    return false;
  }
  uint8_t *buf = static_cast<uint8_t *>(
      heap_caps_malloc(static_cast<size_t>(len), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!buf) {
    buf = static_cast<uint8_t *>(malloc(static_cast<size_t>(len)));
  }
  if (!buf) {
    http.end();
    return false;
  }
  WiFiClient *stream = http.getStreamPtr();
  size_t rd = 0;
  const uint32_t deadline = millis() + kCastaliaBodyReadMs;
  while (rd < static_cast<size_t>(len)) {
    const int avail = stream->available();
    if (avail > 0) {
      const int n = stream->readBytes(buf + rd, static_cast<size_t>(len) - rd);
      if (n > 0) {
        rd += static_cast<size_t>(n);
        continue;
      }
    }
    if (!http.connected() && stream->available() == 0) {
      break;
    }
    if (static_cast<int32_t>(millis() - deadline) >= 0) {
      break;
    }
    yield();
    delay(1);
  }
  http.end();
  if (rd < 8) {
    free(buf);
    return false;
  }
  *out_buf = buf;
  *out_len = rd;
  return true;
}

static int profile_jpeg_draw(JPEGDRAW *pDraw) {
  if (!s_profile_avatar_fb || s_profile_avatar_w <= 0 || s_profile_avatar_h <= 0 || !pDraw) {
    return 0;
  }
  for (int row = 0; row < pDraw->iHeight; ++row) {
    uint16_t *dst = s_profile_avatar_fb + (pDraw->y + row) * s_profile_avatar_w + pDraw->x;
    const uint16_t *src = pDraw->pPixels + row * pDraw->iWidth;
    memcpy(dst, src, static_cast<size_t>(pDraw->iWidth) * sizeof(uint16_t));
  }
  return 1;
}

static bool profile_decode_avatar_jpeg(const uint8_t *data, size_t len) {
  profile_free_avatar_fb();
  JPEGDEC jpg;
  if (jpg.openRAM(const_cast<uint8_t *>(data), static_cast<int>(len), profile_jpeg_draw) != 1) {
    return false;
  }
  const int w = jpg.getWidth();
  const int h = jpg.getHeight();
  if (w <= 0 || h <= 0 || w > 256 || h > 256) {
    jpg.close();
    return false;
  }
  s_profile_avatar_w = w;
  s_profile_avatar_h = h;
  const size_t px = static_cast<size_t>(w) * static_cast<size_t>(h);
  s_profile_avatar_fb = static_cast<uint16_t *>(
      heap_caps_malloc(px * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!s_profile_avatar_fb) {
    s_profile_avatar_fb = static_cast<uint16_t *>(malloc(px * sizeof(uint16_t)));
  }
  if (!s_profile_avatar_fb) {
    jpg.close();
    profile_free_avatar_fb();
    return false;
  }
  memset(s_profile_avatar_fb, 0, px * sizeof(uint16_t));
  jpg.setPixelType(RGB565_BIG_ENDIAN);
  if (jpg.decode(0, 0, 0) != 1) {
    jpg.close();
    profile_free_avatar_fb();
    return false;
  }
  jpg.close();
  s_profile_avatar_ready = true;
  return true;
}

static bool castalia_fetch_profile_http() {
  if (s_refresh[0] == '\0' || strlen(MYNAH_SUPABASE_URL) == 0) {
    return false;
  }
  if (access_token_stale() || access_token_dead()) {
    if (!refresh_session_http()) {
      return false;
    }
  }
  char base[160];
  strncpy(base, MYNAH_SUPABASE_URL, sizeof(base) - 1);
  base[sizeof(base) - 1] = '\0';
  trim_supabase_url(base, sizeof(base));
  char url[220];
  snprintf(url, sizeof(url), "%s/auth/v1/user", base);
  if (!http_get_authed(url, s_http_json_buf, sizeof(s_http_json_buf))) {
    ESP_LOGW(TAG, "profile user fetch failed");
    return false;
  }
  char name[sizeof(s_profile_name)] = "";
  if (!profile_pick_name_from_user_json(s_http_json_buf, name, sizeof(name))) {
    ESP_LOGW(TAG, "profile name missing in user JSON");
    return false;
  }
  strncpy(s_profile_name, name, sizeof(s_profile_name) - 1);
  s_profile_name[sizeof(s_profile_name) - 1] = '\0';
  profile_compute_initials(s_profile_name);
  s_profile_name_ready = true;

  char avatar_url[sizeof(s_profile_avatar_url)] = "";
  if (profile_pick_avatar_url(s_http_json_buf, avatar_url, sizeof(avatar_url))) {
    strncpy(s_profile_avatar_url, avatar_url, sizeof(s_profile_avatar_url) - 1);
    s_profile_avatar_url[sizeof(s_profile_avatar_url) - 1] = '\0';
    char fetch_url[sizeof(s_profile_avatar_url) + 16];
    snprintf(fetch_url, sizeof(fetch_url), "%s", s_profile_avatar_url);
    if (strstr(fetch_url, "googleusercontent.com") != nullptr && strchr(fetch_url, '?') == nullptr) {
      strncat(fetch_url, "=s96-c", sizeof(fetch_url) - strlen(fetch_url) - 1);
    }
    uint8_t *img = nullptr;
    size_t img_len = 0;
    if (http_download_binary(fetch_url, &img, &img_len) && img) {
      if (!profile_decode_avatar_jpeg(img, img_len)) {
        ESP_LOGW(TAG, "avatar JPEG decode failed");
      }
      free(img);
    }
  }
  snprintf(s_status, sizeof(s_status), "Signed in");
  return true;
}

static bool castalia_pair_start_http() {
  char base[160];
  strncpy(base, MYNAH_SUPABASE_URL, sizeof(base) - 1);
  base[sizeof(base) - 1] = '\0';
  trim_supabase_url(base, sizeof(base));
  char url[220];
  snprintf(url, sizeof(url), "%s/functions/v1/mynah-castalia-link/start", base);

  int http_code = -1;
  if (!http_post_json(url, "{}", s_http_json_buf, sizeof(s_http_json_buf), &http_code)) {
    if (http_code == 404) {
      snprintf(s_status, sizeof(s_status), "Deploy mynah-castalia-link");
    } else if (http_code > 0) {
      snprintf(s_status, sizeof(s_status), "Pair start failed (%d)", http_code);
    } else {
      snprintf(s_status, sizeof(s_status), "Pair start failed");
    }
    return false;
  }

  if (!extract_json_string_field(s_http_json_buf, "pair_id", s_pair_id, sizeof(s_pair_id)) ||
      !extract_json_string_field(s_http_json_buf, "pair_secret", s_pair_secret, sizeof(s_pair_secret))) {
    snprintf(s_status, sizeof(s_status), "Bad pair response");
    return false;
  }
  build_signin_url();
  (void)pm_castalia_encode_qr_cache();
  snprintf(s_status, sizeof(s_status), "Scan with phone");
  s_last_poll_ms = millis();
  s_warmup_requested = false;
  return true;
}

void pm_castalia_warmup_after_wifi() {
  pm_castalia_auth_init();
  if (pm_castalia_has_session()) {
    profile_request_fetch();
    return;
  }
  if (!pm_wifi_connected()) {
    return;
  }
  if (strlen(MYNAH_SUPABASE_URL) == 0 || strlen(MYNAH_SUPABASE_ANON_KEY) == 0) {
    return;
  }
  if (s_signin_url[0] != '\0' && s_qr_modules_valid) {
    return;
  }
  s_warmup_requested = true;
  s_pair_start_pending = true;
}

void pm_castalia_on_face_enter() {
  pm_castalia_auth_init();
  s_pair_start_pending = false;
  s_last_poll_ms = 0;

  if (pm_castalia_has_session()) {
    snprintf(s_status, sizeof(s_status), s_profile_name_ready ? "Signed in" : "Loading profile…");
    if (!s_profile_name_ready) {
      profile_request_fetch();
    }
    return;
  }
  if (!pm_wifi_connected()) {
    snprintf(s_status, sizeof(s_status), "WiFi needed");
    s_pair_id[0] = '\0';
    s_pair_secret[0] = '\0';
    s_signin_url[0] = '\0';
    invalidate_qr_cache();
    return;
  }
  if (strlen(MYNAH_SUPABASE_URL) == 0 || strlen(MYNAH_SUPABASE_ANON_KEY) == 0) {
    snprintf(s_status, sizeof(s_status), "Set MYNAH_SUPABASE_*");
    return;
  }
  if (s_signin_url[0] != '\0' && s_qr_modules_valid) {
    snprintf(s_status, sizeof(s_status), "Scan with phone");
    return;
  }
  s_pair_id[0] = '\0';
  s_pair_secret[0] = '\0';
  s_signin_url[0] = '\0';
  invalidate_qr_cache();
  s_pair_start_pending = true;
  snprintf(s_status, sizeof(s_status), "Connecting...");
}

bool pm_castalia_tick_background_pairing() {
  if (!s_warmup_requested && !s_pair_start_pending) {
    return false;
  }
  if (pm_castalia_has_session()) {
    s_warmup_requested = false;
    s_pair_start_pending = false;
    return false;
  }
  return pm_castalia_tick_pair_start();
}

void pm_castalia_note_qr_drawn() {
  s_poll_quiet_until_ms = millis() + kCastaliaPollQuietAfterQrMs;
}

bool pm_castalia_tick_pair_start() {
  if (!s_pair_start_pending) {
    return false;
  }
  s_pair_start_pending = false;
  return castalia_net_run(1, kCastaliaHttpTimeoutMs + 4000u);
}

bool pm_castalia_tick_poll() {
  if (pm_castalia_has_session() || s_pair_id[0] == '\0' || !pm_wifi_connected()) {
    return false;
  }
  const uint32_t now = millis();
  if (now < s_poll_quiet_until_ms) {
    return false;
  }
  if (s_last_poll_ms != 0 && (now - s_last_poll_ms) < kCastaliaPollIntervalMs) {
    return false;
  }
  s_last_poll_ms = now;
  return castalia_net_run(2, kCastaliaHttpTimeoutMs + 4000u);
}

static bool pm_castalia_poll_pairing() {
  pm_castalia_auth_init();
  if (pm_castalia_has_session() || s_pair_id[0] == '\0') {
    return false;
  }
  if (!pm_wifi_connected()) {
    return false;
  }

  url_encode_component(s_pair_id, s_poll_enc_id, sizeof(s_poll_enc_id));
  url_encode_component(s_pair_secret, s_poll_enc_sec, sizeof(s_poll_enc_sec));

  strncpy(s_poll_base, MYNAH_SUPABASE_URL, sizeof(s_poll_base) - 1);
  s_poll_base[sizeof(s_poll_base) - 1] = '\0';
  trim_supabase_url(s_poll_base, sizeof(s_poll_base));
  snprintf(s_poll_url, sizeof(s_poll_url), "%s/functions/v1/mynah-castalia-link/poll?pair_id=%s&pair_secret=%s",
           s_poll_base, s_poll_enc_id, s_poll_enc_sec);

  if (!http_get_text(s_poll_url, s_http_json_buf, sizeof(s_http_json_buf))) {
    return false;
  }

  if (strstr(s_http_json_buf, "\"status\":\"consumed\"") != nullptr ||
      strstr(s_http_json_buf, "\"error\":\"bad_secret\"") != nullptr) {
    snprintf(s_status, sizeof(s_status), "Pair expired — swipe away & back");
    s_pair_id[0] = '\0';
    s_pair_secret[0] = '\0';
    s_signin_url[0] = '\0';
    invalidate_qr_cache();
    return true;
  }
  if (strstr(s_http_json_buf, "\"status\":\"ready\"") == nullptr) {
    return false;
  }

  char at[sizeof(s_access)] = "";
  char rt[sizeof(s_refresh)] = "";
  long exp_in = 3600;
  if (!extract_json_string_field(s_http_json_buf, "access_token", at, sizeof(at)) ||
      !extract_json_string_field(s_http_json_buf, "refresh_token", rt, sizeof(rt))) {
    return false;
  }
  extract_json_long_field(s_http_json_buf, "expires_in", &exp_in);
  if (exp_in < 60) {
    exp_in = 3600;
  }
  strncpy(s_access, at, sizeof(s_access) - 1);
  s_access[sizeof(s_access) - 1] = '\0';
  strncpy(s_refresh, rt, sizeof(s_refresh) - 1);
  s_refresh[sizeof(s_refresh) - 1] = '\0';
  const time_t now_sec = time(nullptr);
  if (now_sec > 100000) {
    s_expires_at_ms = static_cast<uint64_t>(now_sec) * 1000ULL + static_cast<uint64_t>(exp_in) * 1000ULL;
  } else {
    s_expires_at_ms = static_cast<uint64_t>(millis()) + static_cast<uint64_t>(exp_in) * 1000ULL;
  }
  prefs_save_session();
  s_pair_id[0] = '\0';
  s_pair_secret[0] = '\0';
  s_signin_url[0] = '\0';
  invalidate_qr_cache();
  snprintf(s_status, sizeof(s_status), "Loading profile…");
  profile_request_fetch();
  return true;
}

const char *pm_castalia_signin_url_for_qr() {
  return s_signin_url;
}

const char *pm_castalia_status_line() {
  if (s_status[0] != '\0') {
    return s_status;
  }
  return "Castalia";
}

bool pm_castalia_draw_qr(Arduino_Canvas *gfx, int cx, int cy, int max_px) {
  if (!gfx || s_signin_url[0] == '\0') {
    return false;
  }
  if (!pm_castalia_encode_qr_cache()) {
    return false;
  }

  const int size = s_qr_cached_size > 0 ? s_qr_cached_size : qrcodegen_getSize(s_qrcodegen_out);
  if (size <= 0) {
    return false;
  }
  int mod = max_px / size;
  if (mod < 2) {
    mod = 2;
  }
  if (mod > 4) {
    mod = 4;
  }
  s_qr_cached_mod = mod;
  const int total = mod * size;
  const int x0 = cx - total / 2;
  const int y0 = cy - total / 2;
  const uint16_t fg = gfx->color565(8, 8, 12);
  const uint16_t bg = gfx->color565(248, 248, 252);
  gfx->fillRect(x0, y0, total, total, bg);
  for (int y = 0; y < size; ++y) {
    int x = 0;
    while (x < size) {
      const bool on = qrcodegen_getModule(s_qrcodegen_out, x, y);
      int run = 1;
      while (x + run < size && qrcodegen_getModule(s_qrcodegen_out, x + run, y) == on) {
        ++run;
      }
      if (on) {
        gfx->fillRect(x0 + x * mod, y0 + y * mod, run * mod, mod, fg);
      }
      x += run;
    }
    if ((y & 3) == 0) {
      yield();
    }
  }
  pm_castalia_note_qr_drawn();
  return true;
}

const char *pm_castalia_profile_display_name() {
  return s_profile_name_ready ? s_profile_name : "";
}

const char *pm_castalia_profile_initials() {
  return s_profile_initials;
}

bool pm_castalia_tick_fetch_profile() {
  if (!s_profile_fetch_pending || s_profile_fetching) {
    return false;
  }
  if (!pm_wifi_connected() || !pm_castalia_has_session()) {
    s_profile_fetch_pending = false;
    return false;
  }
  s_profile_fetch_pending = false;
  s_profile_fetching = true;
  const bool ok = castalia_net_run(4, kCastaliaHttpTimeoutMs + 16000u);
  s_profile_fetching = false;
  return ok;
}

void pm_castalia_draw_profile_avatar(Arduino_Canvas *gfx, int cx, int cy, int r) {
  if (!gfx || r < 8) {
    return;
  }
  const uint16_t ring = gfx->color565(140, 150, 175);
  const uint16_t fill = gfx->color565(72, 88, 128);
  if (s_profile_avatar_ready && s_profile_avatar_fb && s_profile_avatar_w > 0 && s_profile_avatar_h > 0) {
    const int diam = r * 2;
    const int r2 = r * r;
    for (int dy = -r; dy < r; ++dy) {
      for (int dx = -r; dx < r; ++dx) {
        if (dx * dx + dy * dy > r2) {
          continue;
        }
        const int sx = (dx + r) * s_profile_avatar_w / diam;
        const int sy = (dy + r) * s_profile_avatar_h / diam;
        if (sx >= 0 && sx < s_profile_avatar_w && sy >= 0 && sy < s_profile_avatar_h) {
          gfx->writePixel(cx + dx, cy + dy, s_profile_avatar_fb[sy * s_profile_avatar_w + sx]);
        }
      }
    }
    gfx->drawCircle(cx, cy, r, ring);
    return;
  }
  gfx->fillCircle(cx, cy, r, fill);
  gfx->drawCircle(cx, cy, r, ring);
  const char *ini = s_profile_initials[0] ? s_profile_initials : "?";
  gfx->setTextSize(3, 3);
  int16_t x1 = 0;
  int16_t y1 = 0;
  uint16_t tw = 0;
  uint16_t th = 0;
  gfx->getTextBounds(ini, 0, 0, &x1, &y1, &tw, &th);
  gfx->setCursor(cx - static_cast<int>(tw) / 2, cy - static_cast<int>(th) / 2);
  gfx->setTextColor(gfx->color565(235, 240, 255));
  gfx->print(ini);
}
