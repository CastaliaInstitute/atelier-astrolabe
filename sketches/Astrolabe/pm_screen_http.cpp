#include "pm_screen_http.h"

#include <WebServer.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Update.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <cstring>
#include <mbedtls/sha256.h>

#include "Arduino_GFX_Library.h"
#include "esp32-hal-tinyusb.h"
#include "esp_heap_caps.h"

#include "pm_config.h"
#include "pm_log.h"
#include "pm_display.h"
#include "pm_remote_control.h"
#include "pm_variant.h"
#include "pm_wifi_ntp.h"
#include "pm_build_info.h"

static WebServer s_server(80);
static PmDisplayCanvas *s_canvas = nullptr;
static bool s_http_started = false;
static uint32_t s_ota_armed_until_ms = 0;
static size_t s_ota_bytes = 0;
static size_t s_ota_total = 0;
static bool s_ota_failed = false;
static char s_ota_status[64] = "idle";
static char s_ota_integration_url[192] = "";
static char s_ota_manifest_url[208] = "";

static constexpr const char *kOtaNvsNs = "mynah";
static constexpr const char *kOtaLastShaKey = "ota_sha";
static constexpr const char *kOtaLastGitKey = "ota_git";

#ifndef MYNAH_OTA_UPLOAD_KEY
#define MYNAH_OTA_UPLOAD_KEY MYNAH_REMOTE_CONTROL_KEY
#endif

#ifndef MYNAH_REMOTE_CONTROL_KEY
#define MYNAH_REMOTE_CONTROL_KEY ""
#endif

static void put_le32(uint8_t *p, uint32_t v) {
  p[0] = static_cast<uint8_t>(v & 0xffu);
  p[1] = static_cast<uint8_t>((v >> 8) & 0xffu);
  p[2] = static_cast<uint8_t>((v >> 16) & 0xffu);
  p[3] = static_cast<uint8_t>((v >> 24) & 0xffu);
}

static void put_le16(uint8_t *p, uint16_t v) {
  p[0] = static_cast<uint8_t>(v & 0xffu);
  p[1] = static_cast<uint8_t>((v >> 8) & 0xffu);
}

static bool arg_truthy(const char *name) {
  if (!name || !s_server.hasArg(name)) {
    return false;
  }
  const String value = s_server.arg(name);
  return value.length() == 0 || value == "1" || value == "true" || value == "yes";
}

static void handle_root() {
  char html[896];
  snprintf(html, sizeof(html),
           "<!DOCTYPE html><html><head><meta charset=\"utf-8\"><meta name=\"viewport\" "
           "content=\"width=device-width,initial-scale=1\"><title>Astrolabe</title></head>"
           "<body style=\"margin:0;background:#111;color:#ccc;font-family:system-ui,sans-serif;\">"
           "<p style=\"padding:10px;\">Mynah Astrolabe: <a href=\"http://%s/\" "
           "style=\"color:#8cf\">%s</a> <span style=\"color:#778\">mac %s</span> "
           "| <a href=\"/screen.bmp\" style=\"color:#8cf\">screen.bmp</a> "
           "| <a href=\"/screen.bmp?rgb888=1\" style=\"color:#8cf\">24-bit</a> "
           "| <a href=\"/logs\" style=\"color:#8cf\">logs</a> "
           "| <a href=\"/control\" style=\"color:#8cf\">control</a> "
           "| <a href=\"/ota\" style=\"color:#8cf\">ota</a></p>"
           "<img src=\"/screen.bmp\" style=\"width:100%%;max-width:466px;height:auto;display:block;margin:0 auto;\" "
           "alt=\"screen\"></body></html>",
           pm_wifi_mdns_name(), pm_wifi_mdns_name(), pm_wifi_mac_suffix());
  s_server.send(200, "text/html", html);
}

static bool secure_equals(const char *a, const char *b) {
  if (!a || !b) {
    return false;
  }
  const size_t la = strlen(a);
  const size_t lb = strlen(b);
  uint8_t diff = static_cast<uint8_t>(la ^ lb);
  const size_t n = la > lb ? la : lb;
  for (size_t i = 0; i < n; ++i) {
    const uint8_t ca = i < la ? static_cast<uint8_t>(a[i]) : 0;
    const uint8_t cb = i < lb ? static_cast<uint8_t>(b[i]) : 0;
    diff |= static_cast<uint8_t>(ca ^ cb);
  }
  return diff == 0;
}

static bool ota_key_configured(void) { return MYNAH_OTA_UPLOAD_KEY[0] != '\0'; }

static bool ota_key_ok(const String &value) {
  if (!ota_key_configured() || value.length() == 0) {
    return false;
  }
  const char *token = value.c_str();
  constexpr const char *kBearer = "Bearer ";
  if (strncmp(token, kBearer, strlen(kBearer)) == 0) {
    token += strlen(kBearer);
  }
  return secure_equals(token, MYNAH_OTA_UPLOAD_KEY);
}

static bool ota_authorized(void) {
  if (pm_screen_http_ota_armed()) {
    return true;
  }
  if (s_server.hasHeader("X-Astrolabe-Key") && ota_key_ok(s_server.header("X-Astrolabe-Key"))) {
    return true;
  }
  if (s_server.hasHeader("Authorization") && ota_key_ok(s_server.header("Authorization"))) {
    return true;
  }
  return s_server.hasArg("key") && ota_key_ok(s_server.arg("key"));
}

static void ota_set_status(const char *status) {
  strlcpy(s_ota_status, status ? status : "idle", sizeof(s_ota_status));
}

static const char *ota_effective_channel(void) {
  const char *compiled = pm_variant_ota_channel();
  if (compiled && compiled[0] != '\0' && !secure_equals(compiled, "dev")) {
    return compiled;
  }
  const char *platform = pm_variant_device_platform();
  if (secure_equals(platform, "1.85B") || secure_equals(platform, "1.85")) {
    return pm_variant_get() == PmDeviceVariant::SmartSpeaker ? "astrolabe-smart-speaker-185"
                                                            : "astrolabe-astrolabe-185";
  }
  if (secure_equals(platform, "1.45")) {
    return "astrolabe-cameo-145";
  }
  switch (pm_variant_get()) {
    case PmDeviceVariant::Lunasay:
      return "astrolabe-lunasay-175";
    case PmDeviceVariant::Ocarina:
      return "astrolabe-ocarina-175";
    case PmDeviceVariant::Cameo:
      return "astrolabe-cameo-175";
    case PmDeviceVariant::Luopan:
      return "astrolabe-luopan-175";
    case PmDeviceVariant::Enso:
      return "astrolabe-enso-175";
    case PmDeviceVariant::BabelFish:
      return "astrolabe-babel-fish-175";
    case PmDeviceVariant::Pocket:
    case PmDeviceVariant::Astrolabe:
    default:
      return "astrolabe-astrolabe-175";
  }
}

static const char *ota_integration_url(void) {
  if (s_ota_integration_url[0] == '\0') {
    snprintf(s_ota_integration_url, sizeof(s_ota_integration_url), "%s/%s/firmware.bin",
             MYNAH_OTA_INTEGRATION_BASE_URL, ota_effective_channel());
  }
  return s_ota_integration_url;
}

static const char *ota_manifest_url(void) {
  if (s_ota_manifest_url[0] == '\0') {
    snprintf(s_ota_manifest_url, sizeof(s_ota_manifest_url), "%s/%s/manifest.json",
             MYNAH_OTA_INTEGRATION_BASE_URL, ota_effective_channel());
  }
  return s_ota_manifest_url;
}

static bool hex_encode_sha256(const uint8_t digest[32], char *out, size_t cap) {
  if (!out || cap < 65) {
    return false;
  }
  static constexpr char kHex[] = "0123456789abcdef";
  for (size_t i = 0; i < 32; ++i) {
    out[i * 2] = kHex[(digest[i] >> 4) & 0x0f];
    out[i * 2 + 1] = kHex[digest[i] & 0x0f];
  }
  out[64] = '\0';
  return true;
}

static bool sha256_matches(const char *actual, const char *expected) {
  return expected && expected[0] != '\0' && actual && secure_equals(actual, expected);
}

static bool ota_update_from_url(const char *url, const char *expected_sha256 = nullptr) {
#if MYNAH_DEV_INTEGRATION_OTA
  if (!url || url[0] == '\0') {
    ota_set_status("bad URL");
    return false;
  }
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  http.setTimeout(15000);
  if (!http.begin(client, url)) {
    ota_set_status("URL failed");
    return false;
  }
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  ota_set_status("downloading");
  pm_log_printf(false, "ota: integration fetch %s", url);
  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    char status[32];
    snprintf(status, sizeof(status), "HTTP %d", code);
    ota_set_status(status);
    http.end();
    return false;
  }
  const int len = http.getSize();
  if (len <= 0) {
    ota_set_status("no size");
    http.end();
    return false;
  }
  s_ota_bytes = 0;
  s_ota_total = static_cast<size_t>(len);
  if (!Update.begin(static_cast<size_t>(len), U_FLASH)) {
    ota_set_status("begin failed");
    Update.printError(Serial);
    http.end();
    return false;
  }
  WiFiClient *stream = http.getStreamPtr();
  uint8_t buf[1024];
  mbedtls_sha256_context sha_ctx;
  mbedtls_sha256_init(&sha_ctx);
  mbedtls_sha256_starts(&sha_ctx, 0);
  while (http.connected() && s_ota_bytes < s_ota_total) {
    const size_t available = stream->available();
    if (available == 0) {
      delay(1);
      continue;
    }
    const size_t want = available > sizeof(buf) ? sizeof(buf) : available;
    const int got = stream->readBytes(buf, want);
    if (got <= 0) {
      continue;
    }
    mbedtls_sha256_update(&sha_ctx, buf, static_cast<size_t>(got));
    const size_t written = Update.write(buf, static_cast<size_t>(got));
    s_ota_bytes += written;
    if (written != static_cast<size_t>(got)) {
      ota_set_status("write failed");
      Update.printError(Serial);
      Update.abort();
      http.end();
      mbedtls_sha256_free(&sha_ctx);
      return false;
    }
    ota_set_status("downloading");
    yield();
  }
  http.end();
  if (s_ota_bytes != s_ota_total) {
    ota_set_status("short read");
    Update.abort();
    mbedtls_sha256_free(&sha_ctx);
    return false;
  }
  uint8_t digest[32] = {};
  char actual_sha[65] = "";
  mbedtls_sha256_finish(&sha_ctx, digest);
  mbedtls_sha256_free(&sha_ctx);
  hex_encode_sha256(digest, actual_sha, sizeof(actual_sha));
  if (expected_sha256 && expected_sha256[0] != '\0' && !sha256_matches(actual_sha, expected_sha256)) {
    ota_set_status("sha mismatch");
    pm_log_printf(false, "ota: sha mismatch expected=%s actual=%s", expected_sha256, actual_sha);
    Update.abort();
    return false;
  }
  if (!Update.end(true)) {
    ota_set_status("end failed");
    Update.printError(Serial);
    return false;
  }
  ota_set_status("rebooting");
  pm_log_printf(false, "ota: integration update complete bytes=%u", static_cast<unsigned>(s_ota_bytes));
  if (expected_sha256 && expected_sha256[0] != '\0') {
    Preferences pref;
    if (pref.begin(kOtaNvsNs, false)) {
      pref.putString(kOtaLastShaKey, expected_sha256);
      pref.end();
    }
  }
  return true;
#else
  (void)url;
  ota_set_status("dev OTA disabled");
  return false;
#endif
}

static bool http_get_string(const char *url, String *out, size_t max_bytes) {
#if MYNAH_DEV_INTEGRATION_OTA
  if (!url || !out) {
    return false;
  }
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  http.setTimeout(10000);
  if (!http.begin(client, url)) {
    return false;
  }
  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    return false;
  }
  const int len = http.getSize();
  if (len > 0 && static_cast<size_t>(len) > max_bytes) {
    http.end();
    return false;
  }
  String body = http.getString();
  http.end();
  if (body.length() == 0 || body.length() > max_bytes) {
    return false;
  }
  *out = body;
  return true;
#else
  (void)url;
  (void)out;
  (void)max_bytes;
  return false;
#endif
}

static void absolute_ota_url(const char *manifest_url, char *out, size_t cap) {
  if (!out || cap == 0) {
    return;
  }
  out[0] = '\0';
  if (!manifest_url || manifest_url[0] == '\0') {
    snprintf(out, cap, "%s", ota_integration_url());
    return;
  }
  if (strncmp(manifest_url, "https://", 8) == 0 || strncmp(manifest_url, "http://", 7) == 0) {
    snprintf(out, cap, "%s", manifest_url);
    return;
  }
  if (manifest_url[0] == '/') {
    snprintf(out, cap, "https://astrolabe.castalia.institute%s", manifest_url);
    return;
  }
  snprintf(out, cap, "https://astrolabe.castalia.institute/%s", manifest_url);
}

bool pm_screen_http_ota_auto_check(void) {
#if MYNAH_DEV_INTEGRATION_OTA
  if (!pm_wifi_connected()) {
    ota_set_status("auto no wifi");
    return false;
  }
  ota_set_status("auto checking");
  pm_log_printf(false, "ota: auto check %s", ota_manifest_url());

  String body;
  if (!http_get_string(ota_manifest_url(), &body, 8192)) {
    ota_set_status("manifest failed");
    pm_log_printf(false, "ota: manifest fetch failed");
    return false;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    ota_set_status("manifest bad");
    pm_log_printf(false, "ota: manifest parse failed %s", err.c_str());
    return false;
  }

  const char *channel = doc["ota_channel"] | "";
  const char *sha = doc["sha256"] | "";
  const char *git_sha = doc["git_sha"] | "";
  const char *firmware_url = doc["firmware_url"] | "";
  if (!secure_equals(channel, ota_effective_channel()) || sha[0] == '\0') {
    ota_set_status("manifest mismatch");
    pm_log_printf(false, "ota: manifest mismatch channel=%s sha=%s", channel, sha);
    return false;
  }

  Preferences pref;
  String last_sha;
  if (pref.begin(kOtaNvsNs, false)) {
    last_sha = pref.getString(kOtaLastShaKey, "");
    pref.end();
  }
  if (last_sha.length() > 0 && secure_equals(last_sha.c_str(), sha)) {
    ota_set_status("auto current");
    pm_log_printf(false, "ota: auto current sha=%s", sha);
    return false;
  }
  if (git_sha[0] != '\0' && secure_equals(git_sha, PM_BUILD_GIT_SHA_FULL)) {
    if (pref.begin(kOtaNvsNs, false)) {
      pref.putString(kOtaLastShaKey, sha);
      pref.putString(kOtaLastGitKey, git_sha);
      pref.end();
    }
    ota_set_status("auto current");
    pm_log_printf(false, "ota: auto current git=%s sha=%s", git_sha, sha);
    return false;
  }

  char url[240];
  absolute_ota_url(firmware_url, url, sizeof(url));
  if (url[0] == '\0') {
    snprintf(url, sizeof(url), "%s", ota_integration_url());
  }
  pm_log_printf(false, "ota: auto install git=%s sha=%s url=%s", git_sha, sha, url);
  const bool ok = ota_update_from_url(url, sha);
  if (ok && pref.begin(kOtaNvsNs, false)) {
    pref.putString(kOtaLastShaKey, sha);
    if (git_sha[0] != '\0') {
      pref.putString(kOtaLastGitKey, git_sha);
    }
    pref.end();
  }
  return ok;
#else
  ota_set_status("dev OTA disabled");
  return false;
#endif
}

static void handle_ota_get() {
  char chunk[512];
  const String ip = WiFi.localIP().toString();
  s_server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  s_server.send(200, "text/html; charset=utf-8", "");
  s_server.sendContent(
      "<!DOCTYPE html><html><head><meta charset=\"utf-8\"><meta name=\"viewport\" "
      "content=\"width=device-width,initial-scale=1\"><title>Astrolabe OTA</title></head>"
      "<body style=\"margin:0;background:#101217;color:#d7dde8;font-family:system-ui,sans-serif;\">"
      "<main style=\"max-width:520px;margin:0 auto;padding:22px;\">"
      "<p><a href=\"/\" style=\"color:#8cf\">screen</a></p>"
      "<h1 style=\"font-size:22px;\">Astrolabe OTA</h1>");
  snprintf(chunk, sizeof(chunk),
           "<p>Variant: <b>%s %s</b><br>Channel: <b>%s</b><br>Address: <b>http://%s/ota</b></p>",
           pm_variant_label(pm_variant_get()), pm_variant_device_platform(), ota_effective_channel(),
           ip.c_str());
  s_server.sendContent(chunk);
  snprintf(chunk, sizeof(chunk), "<p>Status: <b>%s</b><br>Physical arm: <b>%s</b>%s</p>",
           s_ota_status, pm_screen_http_ota_armed() ? "armed" : "locked",
           ota_key_configured() ? "<br>Key upload: <b>enabled</b>" : "<br>Key upload: <b>not configured</b>");
  s_server.sendContent(chunk);
  s_server.sendContent(
           "<form method=\"POST\" action=\"/ota\" enctype=\"multipart/form-data\">"
           "<p><input type=\"file\" name=\"firmware\" accept=\".bin\" required></p>"
           "<p><input type=\"password\" name=\"key\" placeholder=\"upload key (optional when armed)\" "
           "style=\"width:100%;box-sizing:border-box;padding:10px;background:#1a1f2a;color:#fff;border:1px solid #3a4252;\"></p>"
           "<p><button type=\"submit\" style=\"padding:10px 14px;\">Upload firmware</button></p>"
           "</form>"
           "<form method=\"POST\" action=\"/ota/integration\">"
           "<p><button type=\"submit\" style=\"padding:10px 14px;background:#203247;color:#e8f2ff;border:1px solid #4d7196;\">"
           "Install latest integration build</button></p>"
           "</form>");
  snprintf(chunk, sizeof(chunk), "<p style=\"word-break:break-all;color:#9aa4b6;\">Integration URL: %s</p>",
           ota_integration_url());
  s_server.sendContent(chunk);
  s_server.sendContent(
      "<p style=\"color:#9aa4b6;\">Tap Settings - OTA on the device to arm browser upload, "
      "or set MYNAH_OTA_UPLOAD_KEY / MYNAH_REMOTE_CONTROL_KEY and submit the key.</p>"
      "</main></body></html>");
  s_server.sendContent("");
}

static void handle_ota_integration_post() {
  if (!ota_authorized()) {
    ota_set_status("locked");
    s_server.send(401, "text/plain", "OTA integration locked; arm device or provide key\n");
    return;
  }
  s_ota_armed_until_ms = 0;
  s_server.send(200, "text/plain", "Starting integration OTA; device will reboot on success\n");
  delay(100);
  if (ota_update_from_url(ota_integration_url())) {
    delay(200);
    ESP.restart();
  }
}

static void handle_ota_post_done() {
  if (s_ota_failed || Update.hasError()) {
    ota_set_status("upload failed");
    s_server.send(500, "text/plain", "OTA failed\n");
    return;
  }
  ota_set_status("rebooting");
  s_server.send(200, "text/plain", "OTA OK, rebooting\n");
  pm_log_printf(false, "ota: update complete bytes=%u", static_cast<unsigned>(s_ota_bytes));
  delay(200);
  ESP.restart();
}

static void handle_ota_upload() {
  HTTPUpload &upload = s_server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    s_ota_failed = false;
    s_ota_bytes = 0;
    s_ota_total = 0;
    if (!ota_authorized()) {
      s_ota_failed = true;
      ota_set_status("locked");
      pm_log_printf(false, "ota: rejected unauthorized upload");
      return;
    }
    s_ota_armed_until_ms = 0;
    ota_set_status("starting");
    pm_log_printf(false, "ota: upload start %s", upload.filename.c_str());
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
      s_ota_failed = true;
      ota_set_status("begin failed");
      Update.printError(Serial);
      return;
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (s_ota_failed) {
      return;
    }
    s_ota_total = upload.totalSize;
    const size_t written = Update.write(upload.buf, upload.currentSize);
    s_ota_bytes += written;
    if (written != upload.currentSize) {
      s_ota_failed = true;
      ota_set_status("write failed");
      Update.printError(Serial);
      return;
    }
    ota_set_status("uploading");
  } else if (upload.status == UPLOAD_FILE_END) {
    if (s_ota_failed) {
      Update.abort();
      return;
    }
    s_ota_total = upload.totalSize;
    if (!Update.end(true)) {
      s_ota_failed = true;
      ota_set_status("end failed");
      Update.printError(Serial);
      return;
    }
    ota_set_status("uploaded");
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    s_ota_failed = true;
    ota_set_status("aborted");
    Update.abort();
  }
}

static void http_send_log_chunk(const char *data, size_t len) {
  if (data && len > 0) {
    s_server.sendContent(data, len);
  }
}

static void handle_logs_txt() {
  if (!pm_wifi_connected()) {
    s_server.send(503, "text/plain", "logs unavailable");
    return;
  }
  s_server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  s_server.send(200, "text/plain", "");
  pm_log_stream_to_http(http_send_log_chunk);
  s_server.sendContent("");
}

static void handle_logs() {
  if (!pm_wifi_connected()) {
    s_server.send(503, "text/plain", "logs unavailable");
    return;
  }
  s_server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  s_server.send(200, "text/html; charset=utf-8", "");
  s_server.sendContent(
      "<!DOCTYPE html><html><head><meta charset=\"utf-8\"><meta name=\"viewport\" "
      "content=\"width=device-width,initial-scale=1\"><meta http-equiv=\"refresh\" content=\"2\">"
      "<title>Astrolabe Logs</title></head><body style=\"margin:0;background:#0b0c10;color:#d7dde8;"
      "font:13px ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;\">"
      "<div style=\"position:sticky;top:0;background:#151821;padding:8px 10px;\">"
      "<a href=\"/\" style=\"color:#8cf\">screen</a> <a href=\"/logs.txt\" style=\"color:#8cf\">logs.txt</a>"
      "</div><pre style=\"white-space:pre-wrap;margin:0;padding:10px;\">");
  pm_log_stream_to_http(http_send_log_chunk);
  s_server.sendContent("</pre></body></html>");
  s_server.sendContent("");
}

static void handle_bootloader() {
  pm_log_printf(false, "http: entering USB CDC bootloader");
  s_server.send(200, "text/plain", "entering bootloader\n");
  delay(100);
  usb_persist_restart(RESTART_BOOTLOADER);
}

static void handle_screen_bmp() {
  if (!s_canvas || !pm_wifi_connected()) {
    s_server.send(503, "text/plain", "screen unavailable");
    return;
  }
  uint16_t *fb = s_canvas->getFramebuffer();
  if (!fb) {
    s_server.send(503, "text/plain", "no framebuffer");
    return;
  }

  const int32_t w = s_canvas->width();
  const int32_t h = s_canvas->height();
  if (w <= 0 || h <= 0 || w > 1024 || h > 1024) {
    s_server.send(500, "text/plain", "bad size");
    return;
  }

  if (!arg_truthy("rgb888") && !arg_truthy("24")) {
    const uint32_t row_stride = ((static_cast<uint32_t>(w) * 16u + 31u) / 32u) * 4u;
    const uint32_t pixel_bytes = row_stride * static_cast<uint32_t>(h);
    const uint32_t file_size = 70u + pixel_bytes;
    uint8_t header[70] = {};
    header[0] = 'B';
    header[1] = 'M';
    put_le32(header + 2, file_size);
    put_le32(header + 10, 70u);
    put_le32(header + 14, 40u);
    put_le32(header + 18, static_cast<uint32_t>(w));
    put_le32(header + 22, static_cast<uint32_t>(h));
    put_le16(header + 26, 1u);
    put_le16(header + 28, 16u);
    put_le32(header + 30, 3u);  // BI_BITFIELDS
    put_le32(header + 34, pixel_bytes);
    put_le32(header + 54, 0x0000F800u);
    put_le32(header + 58, 0x000007E0u);
    put_le32(header + 62, 0x0000001Fu);
    put_le32(header + 66, 0x00000000u);

    constexpr int32_t kRowsPerChunk = 16;
    const size_t chunk_bytes = static_cast<size_t>(row_stride) * kRowsPerChunk;
    uint8_t *chunk = static_cast<uint8_t *>(heap_caps_malloc(chunk_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (!chunk) {
      chunk = static_cast<uint8_t *>(malloc(chunk_bytes));
    }
    if (!chunk) {
      s_server.send(500, "text/plain", "chunk alloc failed");
      return;
    }

    s_server.setContentLength(file_size);
    s_server.send(200, "image/bmp", "");
    s_server.sendContent(reinterpret_cast<const char *>(header), sizeof(header));
    for (int32_t yi = 0; yi < h;) {
      const int32_t rows = (h - yi) > kRowsPerChunk ? kRowsPerChunk : (h - yi);
      uint8_t *out = chunk;
      for (int32_t r = 0; r < rows; ++r, ++yi) {
        const int32_t y = h - 1 - yi;
        const uint16_t *src = fb + static_cast<int32_t>(y) * w;
        uint8_t *dst = out;
        for (int32_t x = 0; x < w; ++x) {
          const uint16_t c = src[x];
          *dst++ = static_cast<uint8_t>(c & 0xffu);
          *dst++ = static_cast<uint8_t>(c >> 8);
        }
        while (static_cast<uint32_t>(dst - out) < row_stride) {
          *dst++ = 0;
        }
        out += row_stride;
      }
      s_server.sendContent(reinterpret_cast<const char *>(chunk), static_cast<size_t>(rows) * row_stride);
      delay(0);
    }
    free(chunk);
    return;
  }

  const uint32_t row_stride = ((static_cast<uint32_t>(w) * 24u + 31u) / 32u) * 4u;
  const uint32_t pixel_bytes = row_stride * static_cast<uint32_t>(h);
  const uint32_t file_size = 54u + pixel_bytes;

  uint8_t *buf = static_cast<uint8_t *>(
      heap_caps_malloc(file_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!buf) {
    buf = static_cast<uint8_t *>(malloc(file_size));
  }
  if (!buf) {
    s_server.send(500, "text/plain", "alloc failed");
    return;
  }

  std::memset(buf, 0, file_size);
  buf[0] = 'B';
  buf[1] = 'M';
  put_le32(buf + 2, file_size);
  put_le32(buf + 10, 54u);
  put_le32(buf + 14, 40u);
  put_le32(buf + 18, static_cast<uint32_t>(w));
  put_le32(buf + 22, static_cast<uint32_t>(h));
  put_le16(buf + 26, 1u);
  put_le16(buf + 28, 24u);
  put_le32(buf + 34, pixel_bytes);

  uint16_t *snap = static_cast<uint16_t *>(
      heap_caps_malloc(static_cast<size_t>(w) * static_cast<size_t>(h) * 2u,
                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!snap) {
    snap = static_cast<uint16_t *>(malloc(static_cast<size_t>(w) * static_cast<size_t>(h) * 2u));
  }
  if (snap) {
    std::memcpy(snap, fb, static_cast<size_t>(w) * static_cast<size_t>(h) * 2u);
  } else {
    snap = fb;
  }

  uint8_t *pix = buf + 54;
  for (int32_t yi = 0; yi < h; ++yi) {
    const int32_t y = h - 1 - yi;
    uint8_t *dst = pix + static_cast<uint32_t>(yi) * row_stride;
    const uint16_t *src = snap + static_cast<int32_t>(y) * w;
    for (int32_t x = 0; x < w; ++x) {
      const uint16_t c = src[x];
      const unsigned r5 = (c >> 11) & 0x1fu;
      const unsigned g6 = (c >> 5) & 0x3fu;
      const unsigned b5 = c & 0x1fu;
      *dst++ = static_cast<uint8_t>((b5 * 255u + 15u) / 31u);
      *dst++ = static_cast<uint8_t>((g6 * 255u + 31u) / 63u);
      *dst++ = static_cast<uint8_t>((r5 * 255u + 15u) / 31u);
    }
    for (uint32_t pad = static_cast<uint32_t>(w) * 3u; pad < row_stride; ++pad) {
      *dst++ = 0;
    }
  }

  if (snap != fb) {
    free(snap);
  }

  s_server.setContentLength(file_size);
  s_server.send(200, "image/bmp", "");
  s_server.sendContent(reinterpret_cast<const char *>(buf), file_size);
  free(buf);
}

void pm_screen_http_begin(PmDisplayCanvas *canvas) {
  s_canvas = canvas;
  if (s_http_started || !canvas || !pm_wifi_connected()) {
    return;
  }
  s_server.on("/", HTTP_GET, handle_root);
  s_server.on("/screen.bmp", HTTP_GET, handle_screen_bmp);
  s_server.on("/logs", HTTP_GET, handle_logs);
  s_server.on("/logs.txt", HTTP_GET, handle_logs_txt);
  s_server.on("/ota", HTTP_GET, handle_ota_get);
  s_server.on("/ota", HTTP_POST, handle_ota_post_done, handle_ota_upload);
  s_server.on("/ota/integration", HTTP_POST, handle_ota_integration_post);
  s_server.on("/bootloader", HTTP_POST, handle_bootloader);
  pm_remote_control_register_http(s_server);
  s_server.begin();
  s_http_started = true;
  pm_log_printf(false, "http: ready http://%s/ ip=%s", pm_wifi_mdns_name(),
                WiFi.localIP().toString().c_str());
  Serial.printf("Screen over WiFi: http://%s/ or http://%s/  (GET /screen.bmp)\n",
                pm_wifi_mdns_name(), WiFi.localIP().toString().c_str());
}

void pm_screen_http_ota_arm(uint32_t duration_ms) {
  s_ota_armed_until_ms = millis() + duration_ms;
  ota_set_status("armed");
}

bool pm_screen_http_ota_armed(void) {
  return s_ota_armed_until_ms != 0 && static_cast<int32_t>(s_ota_armed_until_ms - millis()) > 0;
}

const char *pm_screen_http_ota_status(void) { return s_ota_status; }

size_t pm_screen_http_ota_bytes(void) { return s_ota_bytes; }

size_t pm_screen_http_ota_total(void) { return s_ota_total; }

const char *pm_screen_http_ota_integration_url(void) { return ota_integration_url(); }

void pm_screen_http_loop() {
  if (!s_http_started) {
    if (s_canvas && pm_wifi_connected()) {
      pm_screen_http_begin(s_canvas);
    }
    return;
  }
  if (!pm_wifi_connected()) {
    return;
  }
  s_server.handleClient();
}
