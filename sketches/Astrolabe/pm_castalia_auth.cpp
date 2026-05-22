#include "pm_castalia_auth.h"

#include <ctype.h>
#include <cstring>
#include <ctime>

#include "Arduino_GFX_Library.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pm_config.h"
#include "pm_http.h"
#include "pm_nvs.h"
#include "pm_speaker.h"
#include "pm_wifi_ntp.h"

extern "C" {
#include "third_party/qrcodegen/qrcodegen.h"
}

static const char *TAG = "pm_castalia";
static const char kNs[] = "castalia";
static const char kAt[] = "access_token";
static const char kRt[] = "refresh_token";
static const char kEx[] = "exp_ms";
static const char kIndividual[] = "individual";

static bool s_inited = false;

static char s_pair_id[48] = "";
static char s_pair_secret[96] = "";
static char s_signin_url[512] = "";
static char s_status[120] = "";
static char s_individual_id[48] = "";
static char s_repo_name[72] = "";

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
static constexpr uint32_t kCastaliaNetTaskStack = 16384;

static TaskHandle_t s_net_task = nullptr;
static volatile bool s_net_busy = false;
static volatile bool s_net_done = false;
static volatile bool s_net_ok = false;
static volatile uint8_t s_net_op = 0; /** 1 = pair start, 2 = poll, 3 = refresh session */

static char s_poll_enc_id[80];
static char s_poll_enc_sec[160];
static char s_poll_url[400];
static char s_poll_base[160];
/** Skip qrcodegen_encodeText when sign-in URL unchanged (full_paint runs every second). */
static char s_qr_cached_url[sizeof(s_signin_url)] = "";
static char s_qr_failed_url[sizeof(s_signin_url)] = "";
static bool s_qr_modules_valid = false;

static constexpr uint32_t kCastaliaHttpTimeoutMs = 12000;
static constexpr uint32_t kCastaliaPollIntervalMs = 2000;

static void trim_supabase_url(char *url, size_t cap) {
  if (!url || cap == 0) {
    return;
  }
  while (strlen(url) > 0 && url[strlen(url) - 1] == '/') {
    url[strlen(url) - 1] = '\0';
  }
}

static bool valid_profile_char(char c) {
  return isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_';
}

static void normalize_individual(const char *in, char *out, size_t cap) {
  if (!out || cap == 0) {
    return;
  }
  out[0] = '\0';
  if (!in) {
    return;
  }
  while (*in == ' ') {
    ++in;
  }
  size_t o = 0;
  for (size_t i = 0; in[i] && o + 1 < cap; ++i) {
    if (in[i] == ' ') {
      continue;
    }
    if (!valid_profile_char(in[i])) {
      break;
    }
    out[o++] = in[i];
  }
  out[o] = '\0';
}

static void rebuild_repo_name() {
  const char *id = s_individual_id[0] != '\0' ? s_individual_id : MYNAH_CASTALIA_INDIVIDUAL_DEFAULT;
  snprintf(s_repo_name, sizeof(s_repo_name), "%s%s", MYNAH_CASTALIA_REPO_PREFIX, id);
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

static void castalia_anon_auth(char *auth, size_t auth_cap) {
  if (!auth || auth_cap == 0) {
    return;
  }
  snprintf(auth, auth_cap, "Bearer %s", MYNAH_SUPABASE_ANON_KEY);
}

static void prefs_load() {
  normalize_individual(MYNAH_CASTALIA_INDIVIDUAL_DEFAULT, s_individual_id, sizeof(s_individual_id));
  if (s_individual_id[0] == '\0') {
    strncpy(s_individual_id, "DanielCMcShan", sizeof(s_individual_id) - 1);
    s_individual_id[sizeof(s_individual_id) - 1] = '\0';
  }
  rebuild_repo_name();
  s_access[0] = '\0';
  s_refresh[0] = '\0';
  s_expires_at_ms = 0;
  pm_nvs_get_str(kNs, kAt, s_access, sizeof(s_access), "");
  pm_nvs_get_str(kNs, kRt, s_refresh, sizeof(s_refresh), "");
  char exp_str[24];
  pm_nvs_get_str(kNs, kEx, exp_str, sizeof(exp_str), "0");
  s_expires_at_ms = strtoull(exp_str, nullptr, 10);
  if (pm_nvs_has_key(kNs, kIndividual)) {
    char saved[sizeof(s_individual_id)] = "";
    pm_nvs_get_str(kNs, kIndividual, saved, sizeof(saved), "");
    normalize_individual(saved, s_individual_id, sizeof(s_individual_id));
    if (s_individual_id[0] == '\0') {
      normalize_individual(MYNAH_CASTALIA_INDIVIDUAL_DEFAULT, s_individual_id, sizeof(s_individual_id));
    }
    rebuild_repo_name();
  }
}

static void prefs_save_individual() {
  (void)pm_nvs_set_str(kNs, kIndividual, s_individual_id);
}

static void prefs_save_session() {
  (void)pm_nvs_set_str(kNs, kAt, s_access);
  (void)pm_nvs_set_str(kNs, kRt, s_refresh);
  char expbuf[24];
  snprintf(expbuf, sizeof(expbuf), "%llu", static_cast<unsigned long long>(s_expires_at_ms));
  (void)pm_nvs_set_str(kNs, kEx, expbuf);
}

static void prefs_clear_session() {
  (void)pm_nvs_remove(kNs, kAt);
  (void)pm_nvs_remove(kNs, kRt);
  (void)pm_nvs_remove(kNs, kEx);
  s_access[0] = '\0';
  s_refresh[0] = '\0';
  s_expires_at_ms = 0;
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

  char auth[1560];
  castalia_anon_auth(auth, sizeof(auth));
  const PmHttpHeader headers[] = {
      {"Content-Type", "application/json"},
      {"apikey", MYNAH_SUPABASE_ANON_KEY},
      {"Authorization", auth},
  };
  PmHttpTextResult result = {};
  if (!pm_http_request_text(url, "POST", body, headers, sizeof(headers) / sizeof(headers[0]),
                            s_http_json_buf, sizeof(s_http_json_buf), kCastaliaHttpTimeoutMs,
                            &result)) {
    ESP_LOGW(TAG, "refresh HTTP/read fail %d", result.status_code);
    prefs_clear_session();
    return false;
  }

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
      ESP_LOGW(TAG, "session refresh failed");
      s_access[0] = '\0';
      return false;
    }
  }
  return true;
}

static void invalidate_qr_cache() {
  s_qr_cached_url[0] = '\0';
  s_qr_failed_url[0] = '\0';
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
  if (s_qr_failed_url[0] != '\0' && strcmp(s_signin_url, s_qr_failed_url) == 0) {
    return false;
  }
  if (!qrcodegen_encodeText(s_signin_url, s_qrcodegen_temp, s_qrcodegen_out, qrcodegen_Ecc_LOW, qrcodegen_VERSION_MIN,
                           kCastaliaQrMaxVersion, qrcodegen_Mask_AUTO, true)) {
    ESP_LOGW(TAG, "QR encode failed (url len %u)", static_cast<unsigned>(strlen(s_signin_url)));
    strncpy(s_qr_failed_url, s_signin_url, sizeof(s_qr_failed_url) - 1);
    s_qr_failed_url[sizeof(s_qr_failed_url) - 1] = '\0';
    s_qr_cached_url[0] = '\0';
    s_qr_modules_valid = false;
    s_qr_cached_size = 0;
    s_qr_cached_mod = 0;
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
    } else {
      s_net_ok = false;
    }
    s_net_done = true;
    s_net_busy = false;
  }
}

static void castalia_net_task_ensure() {
  if (s_net_task) {
    return;
  }
  (void)pm_speaker_release_idle_task();
  xTaskCreatePinnedToCore(castalia_net_task, "castalia_net", kCastaliaNetTaskStack, nullptr, 1, &s_net_task, 1);
}

static bool castalia_net_run(uint8_t op, uint32_t timeout_ms) {
  castalia_net_task_ensure();
  if (!s_net_task || s_net_busy) {
    return false;
  }
  s_net_op = op;
  s_net_done = false;
  s_net_busy = true;
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

static bool castalia_net_begin(uint8_t op) {
  castalia_net_task_ensure();
  if (!s_net_task || s_net_busy) {
    return false;
  }
  s_net_op = op;
  s_net_done = false;
  s_net_ok = false;
  s_net_busy = true;
  xTaskNotify(s_net_task, 1, eSetBits);
  return true;
}

static bool castalia_net_collect(uint8_t op, bool *ok) {
  if (s_net_op != op || s_net_busy || !s_net_done) {
    return false;
  }
  if (ok) {
    *ok = s_net_ok;
  }
  s_net_op = 0;
  s_net_done = false;
  return true;
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
  const char *origin = MYNAH_CASTALIA_WEB_ORIGIN;
  char origin_trim[96];
  strncpy(origin_trim, origin, sizeof(origin_trim) - 1);
  origin_trim[sizeof(origin_trim) - 1] = '\0';
  trim_supabase_url(origin_trim, sizeof(origin_trim));
  char enc_device[32];
  char enc_key[200];
  url_encode_component(pm_wifi_mac_suffix(), enc_device, sizeof(enc_device));
  url_encode_component(s_pair_secret, enc_key, sizeof(enc_key));
  snprintf(s_signin_url, sizeof(s_signin_url), "%s/auth/mynah-device/?device=%s&pair=%s&key=%s", origin_trim,
           enc_device, s_pair_id, enc_key);
}

static bool http_post_json(const char *url, const char *body, char *resp, size_t resp_cap, int *http_code_out) {
  if (http_code_out) {
    *http_code_out = -1;
  }
  char auth[1560];
  castalia_anon_auth(auth, sizeof(auth));
  const PmHttpHeader headers[] = {
      {"Content-Type", "application/json"},
      {"apikey", MYNAH_SUPABASE_ANON_KEY},
      {"Authorization", auth},
  };
  PmHttpTextResult result = {};
  const bool ok = pm_http_request_text(url, "POST", body ? body : "{}", headers,
                                       sizeof(headers) / sizeof(headers[0]), resp, resp_cap,
                                       kCastaliaHttpTimeoutMs, &result);
  if (http_code_out) {
    *http_code_out = result.status_code;
  }
  if (!ok) {
    ESP_LOGW(TAG, "POST %s -> %d", url, result.status_code);
    return false;
  }
  return ok;
}

static bool http_get_text(const char *url, char *resp, size_t resp_cap) {
  char auth[1560];
  castalia_anon_auth(auth, sizeof(auth));
  const PmHttpHeader headers[] = {
      {"apikey", MYNAH_SUPABASE_ANON_KEY},
      {"Authorization", auth},
  };
  PmHttpTextResult result = {};
  if (!pm_http_request_text(url, "GET", nullptr, headers, sizeof(headers) / sizeof(headers[0]),
                            resp, resp_cap, kCastaliaHttpTimeoutMs, &result)) {
    ESP_LOGW(TAG, "GET %s -> %d", url, result.status_code);
    return false;
  }
  return true;
}

static bool castalia_pair_start_http() {
  char base[160];
  strncpy(base, MYNAH_SUPABASE_URL, sizeof(base) - 1);
  base[sizeof(base) - 1] = '\0';
  trim_supabase_url(base, sizeof(base));
  char url[220];
  snprintf(url, sizeof(url), "%s/functions/v1/mynah-castalia-link/start", base);

  char repo_full[112];
  pm_castalia_repo_full_name(repo_full, sizeof(repo_full));
  char body[520];
  snprintf(body, sizeof(body),
           "{\"device_kind\":\"astrolabe\",\"device_name\":\"%s\",\"device_id\":\"%s\",\"device_mac\":\"%s\","
           "\"individual_id\":\"%s\","
           "\"settings_source\":\"%s\",\"settings_path\":\"%s\",\"repo_owner\":\"%s\","
           "\"repo_name\":\"%s\",\"individual_repo\":\"%s\"}",
           pm_wifi_mdns_name(), pm_wifi_mac_suffix(), pm_wifi_mac_string(), pm_castalia_individual_id(),
           MYNAH_CASTALIA_SETTINGS_SOURCE, MYNAH_CASTALIA_SETTINGS_PATH, MYNAH_CASTALIA_REPO_OWNER,
           pm_castalia_repo_name(), repo_full);

  int http_code = -1;
  if (!http_post_json(url, body, s_http_json_buf, sizeof(s_http_json_buf), &http_code)) {
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
  snprintf(s_status, sizeof(s_status), "Scan for %s", pm_castalia_individual_id());
  s_last_poll_ms = millis();
  s_warmup_requested = false;
  return true;
}

void pm_castalia_warmup_after_wifi() {
  pm_castalia_auth_init();
  if (pm_castalia_has_session()) {
    return;
  }
  if (!pm_wifi_connected()) {
    return;
  }
  if (strlen(MYNAH_SUPABASE_URL) == 0 || strlen(MYNAH_SUPABASE_ANON_KEY) == 0) {
    return;
  }
  if (s_pair_id[0] != '\0' && s_signin_url[0] != '\0' && s_qr_modules_valid) {
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
    snprintf(s_status, sizeof(s_status), "Linked %s", pm_castalia_individual_id());
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
  if (s_pair_id[0] != '\0' && s_signin_url[0] != '\0') {
    snprintf(s_status, sizeof(s_status), "Scan with phone");
    return;
  }
  s_pair_id[0] = '\0';
  s_pair_secret[0] = '\0';
  s_signin_url[0] = '\0';
  invalidate_qr_cache();
  s_pair_start_pending = true;
  snprintf(s_status, sizeof(s_status), "Pairing...");
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
  bool ok = false;
  if (castalia_net_collect(1, &ok)) {
    snprintf(s_status, sizeof(s_status), "%s", ok ? "Scan with phone" : "Pair start failed");
    return true;
  }
  if (!s_pair_start_pending || s_net_busy) {
    return false;
  }
  s_pair_start_pending = false;
  snprintf(s_status, sizeof(s_status), "Pairing...");
  (void)castalia_net_begin(1);
  return true;
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
  snprintf(s_status, sizeof(s_status), "Linked %s", pm_castalia_individual_id());
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

const char *pm_castalia_individual_id() {
  pm_castalia_auth_init();
  if (s_individual_id[0] == '\0') {
    normalize_individual(MYNAH_CASTALIA_INDIVIDUAL_DEFAULT, s_individual_id, sizeof(s_individual_id));
    rebuild_repo_name();
  }
  return s_individual_id;
}

const char *pm_castalia_repo_name() {
  pm_castalia_auth_init();
  if (s_repo_name[0] == '\0') {
    rebuild_repo_name();
  }
  return s_repo_name;
}

void pm_castalia_repo_full_name(char *out, size_t out_cap) {
  if (!out || out_cap == 0) {
    return;
  }
  snprintf(out, out_cap, "%s/%s", MYNAH_CASTALIA_REPO_OWNER, pm_castalia_repo_name());
}

static bool set_castalia_individual(const char *raw) {
  char id[sizeof(s_individual_id)] = "";
  if (!raw) {
    return false;
  }
  const char *p = raw;
  while (*p == ' ') {
    ++p;
  }
  const char *slash = strrchr(p, '/');
  if (slash) {
    p = slash + 1;
  }
  if (strncmp(p, MYNAH_CASTALIA_REPO_PREFIX, strlen(MYNAH_CASTALIA_REPO_PREFIX)) == 0) {
    p += strlen(MYNAH_CASTALIA_REPO_PREFIX);
  }
  normalize_individual(p, id, sizeof(id));
  if (id[0] == '\0') {
    return false;
  }
  strncpy(s_individual_id, id, sizeof(s_individual_id) - 1);
  s_individual_id[sizeof(s_individual_id) - 1] = '\0';
  rebuild_repo_name();
  prefs_save_individual();
  s_pair_id[0] = '\0';
  s_pair_secret[0] = '\0';
  s_signin_url[0] = '\0';
  invalidate_qr_cache();
  snprintf(s_status, sizeof(s_status), "Profile %s", s_individual_id);
  return true;
}

bool pm_castalia_serial_command(const char *line) {
  if (!line) {
    return false;
  }
  if (strcmp(line, "castalia") == 0 || strcmp(line, "castalia profile") == 0 || strcmp(line, "castalia repo") == 0 ||
      strcmp(line, "castalia individual") == 0) {
    char full[112];
    pm_castalia_repo_full_name(full, sizeof(full));
    Serial.printf("castalia: individual=%s source=%s repo=%s settings=%s\n", pm_castalia_individual_id(), MYNAH_CASTALIA_SETTINGS_SOURCE,
                  full, MYNAH_CASTALIA_SETTINGS_PATH);
    return true;
  }
  if (strcmp(line, "castalia url") == 0 || strcmp(line, "castalia pair") == 0) {
    if (!pm_castalia_has_session()) {
      pm_castalia_on_face_enter();
      pm_castalia_tick_pair_start();
    }
    if (s_signin_url[0] != '\0') {
      Serial.printf("castalia: url=%s\n", s_signin_url);
    } else {
      Serial.printf("castalia: url pending status=%s\n", pm_castalia_status_line());
    }
    return true;
  }
  const char *arg = nullptr;
  if (strncmp(line, "castalia individual ", 20) == 0) {
    arg = line + 20;
  } else if (strncmp(line, "castalia profile ", 17) == 0) {
    arg = line + 17;
  } else if (strncmp(line, "castalia repo ", 14) == 0) {
    arg = line + 14;
  } else {
    return false;
  }
  if (set_castalia_individual(arg)) {
    char full[112];
    pm_castalia_repo_full_name(full, sizeof(full));
    Serial.printf("castalia: saved individual=%s repo=%s source=%s\n", pm_castalia_individual_id(), full, MYNAH_CASTALIA_SETTINGS_SOURCE);
  } else {
    Serial.println("castalia: usage: castalia individual CamilleStMartin");
  }
  return true;
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
