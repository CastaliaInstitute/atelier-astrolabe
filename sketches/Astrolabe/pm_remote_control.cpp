#include "pm_remote_control.h"

#include <ArduinoJson.h>
#include <WebServer.h>
#include <cstring>
#include <Preferences.h>
#include <esp_system.h>

#include "pm_config.h"
#include "pm_log.h"

#if !defined(ASTROLABE_QEMU) && __has_include(<BLEDevice.h>)
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#define PM_REMOTE_BLE 1
#else
#define PM_REMOTE_BLE 0
#endif

#ifndef MYNAH_REMOTE_CONTROL_KEY
#define MYNAH_REMOTE_CONTROL_KEY ""
#endif

namespace {

constexpr size_t kRemoteJsonCap = 1536;
constexpr const char *kRemoteServiceUuid = "7f5a0001-6d55-4f6a-8fd8-a5701abe0001";
constexpr const char *kRemoteCommandUuid = "7f5a0002-6d55-4f6a-8fd8-a5701abe0001";
constexpr const char *kNvsNs = "mynah";
constexpr const char *kNvsRemoteKey = "remote_key";
constexpr uint32_t kDefaultPairSeconds = 120;
constexpr uint32_t kMaxPairSeconds = 600;

PmRemoteCommand s_queue[4];
uint8_t s_read = 0;
uint8_t s_write = 0;
uint8_t s_count = 0;
uint32_t s_seq = 0;
char s_paired_key[65] = "";
char s_pair_code[7] = "";
uint32_t s_pair_until_ms = 0;
bool s_loaded = false;

void remote_load_key(void) {
  if (s_loaded) {
    return;
  }
  s_loaded = true;
  Preferences pref;
  if (!pref.begin(kNvsNs, true)) {
    return;
  }
  const String stored = pref.getString(kNvsRemoteKey, "");
  pref.end();
  strlcpy(s_paired_key, stored.c_str(), sizeof(s_paired_key));
}

void remote_save_key(const char *key) {
  strlcpy(s_paired_key, key ? key : "", sizeof(s_paired_key));
  s_loaded = true;
  Preferences pref;
  if (!pref.begin(kNvsNs, false)) {
    return;
  }
  if (s_paired_key[0]) {
    pref.putString(kNvsRemoteKey, s_paired_key);
  } else {
    pref.remove(kNvsRemoteKey);
  }
  pref.end();
}

bool token_configured(void) {
  remote_load_key();
  return s_paired_key[0] != '\0' || MYNAH_REMOTE_CONTROL_KEY[0] != '\0';
}

bool secure_equals(const char *a, const char *b) {
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

bool auth_value_ok(const char *value) {
  if (!token_configured() || !value || value[0] == '\0') {
    return false;
  }
  const char *token = value;
  constexpr const char *kBearer = "Bearer ";
  if (strncmp(value, kBearer, strlen(kBearer)) == 0) {
    token = value + strlen(kBearer);
  }
  remote_load_key();
  if (s_paired_key[0] != '\0' && secure_equals(token, s_paired_key)) {
    return true;
  }
  return MYNAH_REMOTE_CONTROL_KEY[0] != '\0' && secure_equals(token, MYNAH_REMOTE_CONTROL_KEY);
}

bool http_authorized(WebServer &server) {
  if (server.hasHeader("X-Astrolabe-Key") &&
      auth_value_ok(server.header("X-Astrolabe-Key").c_str())) {
    return true;
  }
  return server.hasHeader("Authorization") &&
         auth_value_ok(server.header("Authorization").c_str());
}

void copy_json_string(JsonVariantConst v, char *out, size_t cap) {
  if (!out || cap == 0) {
    return;
  }
  const char *s = v.is<const char *>() ? v.as<const char *>() : "";
  strlcpy(out, s ? s : "", cap);
}

bool parse_command_json(const char *body, PmRemoteCommand *out, char *err, size_t err_cap) {
  if (!body || !out) {
    strlcpy(err, "missing body", err_cap);
    return false;
  }
  JsonDocument doc;
  DeserializationError jerr = deserializeJson(doc, body);
  if (jerr) {
    strlcpy(err, "bad json", err_cap);
    return false;
  }
  const char *cmd = doc["cmd"] | "";
  if (cmd[0] == '\0') {
    cmd = doc["command"] | "";
  }
  if (cmd[0] == '\0') {
    strlcpy(err, "missing cmd", err_cap);
    return false;
  }

  PmRemoteCommand c;
  copy_json_string(doc["face"], c.face, sizeof(c.face));
  copy_json_string(doc["button"], c.button, sizeof(c.button));
  copy_json_string(doc["mode"], c.mode, sizeof(c.mode));
  copy_json_string(doc["text"], c.text, sizeof(c.text));
  c.x = static_cast<int16_t>(doc["x"] | 233);
  c.y = static_cast<int16_t>(doc["y"] | 233);
  c.duration_ms = doc["durationMs"] | 0u;
  if (c.duration_ms == 0) {
    c.duration_ms = doc["duration_ms"] | 120u;
  }
  c.dwell_ms = doc["dwellMs"] | 0u;
  if (c.dwell_ms == 0) {
    c.dwell_ms = doc["dwell_ms"] | 0u;
  }

  if (strcmp(cmd, "face") == 0) {
    c.type = PmRemoteCommandType::Face;
    if (c.face[0] == '\0') {
      copy_json_string(doc["value"], c.face, sizeof(c.face));
    }
  } else if (strcmp(cmd, "button") == 0 || strcmp(cmd, "press") == 0) {
    c.type = PmRemoteCommandType::Button;
  } else if (strcmp(cmd, "tap") == 0) {
    c.type = PmRemoteCommandType::Tap;
  } else if (strcmp(cmd, "touch_down") == 0 || strcmp(cmd, "touchDown") == 0) {
    c.type = PmRemoteCommandType::TouchDown;
  } else if (strcmp(cmd, "touch_up") == 0 || strcmp(cmd, "touchUp") == 0) {
    c.type = PmRemoteCommandType::TouchUp;
  } else if (strcmp(cmd, "tts") == 0 || strcmp(cmd, "say") == 0) {
    c.type = PmRemoteCommandType::Tts;
  } else if (strcmp(cmd, "tour") == 0) {
    c.type = PmRemoteCommandType::Tour;
  } else if (strcmp(cmd, "stop") == 0 || strcmp(cmd, "abort") == 0) {
    c.type = PmRemoteCommandType::Stop;
  } else {
    strlcpy(err, "unknown cmd", err_cap);
    return false;
  }

  if (c.type == PmRemoteCommandType::Tts && c.text[0] == '\0') {
    strlcpy(err, "missing text", err_cap);
    return false;
  }
  if (c.type == PmRemoteCommandType::Button && c.button[0] == '\0') {
    strlcpy(err, "missing button", err_cap);
    return false;
  }
  *out = c;
  return true;
}

void send_json(WebServer &server, int code, const char *body) {
  server.send(code, "application/json", body ? body : "{}");
}

bool pairing_active(void) {
  return s_pair_code[0] != '\0' && s_pair_until_ms != 0 &&
         static_cast<int32_t>(millis() - s_pair_until_ms) < 0;
}

void pairing_clear(void) {
  s_pair_code[0] = '\0';
  s_pair_until_ms = 0;
}

void random_hex(char *out, size_t cap, size_t bytes) {
  static const char kHex[] = "0123456789abcdef";
  if (!out || cap < bytes * 2 + 1) {
    return;
  }
  for (size_t i = 0; i < bytes; ++i) {
    const uint8_t b = static_cast<uint8_t>(esp_random() & 0xffu);
    out[i * 2] = kHex[b >> 4];
    out[i * 2 + 1] = kHex[b & 0x0f];
  }
  out[bytes * 2] = '\0';
}

void pairing_begin(uint32_t seconds) {
  if (seconds == 0) {
    seconds = kDefaultPairSeconds;
  }
  if (seconds > kMaxPairSeconds) {
    seconds = kMaxPairSeconds;
  }
  const uint32_t code = 100000u + (esp_random() % 900000u);
  snprintf(s_pair_code, sizeof(s_pair_code), "%06lu", static_cast<unsigned long>(code));
  s_pair_until_ms = millis() + seconds * 1000u;
}

}  // namespace

bool pm_remote_control_enabled(void) {
  return token_configured();
}

uint32_t pm_remote_control_sequence(void) {
  return s_seq;
}

bool pm_remote_control_serial_command(const char *line) {
  if (!line) {
    return false;
  }
  if (strcmp(line, "remote") == 0 || strcmp(line, "remote status") == 0) {
    remote_load_key();
    const uint32_t remain = pairing_active() ? (s_pair_until_ms - millis()) / 1000u : 0u;
    Serial.printf("remote: enabled=%d paired=%d static=%d pairing=%d remain=%lu seq=%lu\n",
                  pm_remote_control_enabled() ? 1 : 0, s_paired_key[0] ? 1 : 0,
                  MYNAH_REMOTE_CONTROL_KEY[0] ? 1 : 0, pairing_active() ? 1 : 0,
                  static_cast<unsigned long>(remain), static_cast<unsigned long>(s_seq));
    return true;
  }
  if (strncmp(line, "remote pair", 11) == 0 && (line[11] == '\0' || line[11] == ' ')) {
    const char *p = line + 11;
    while (*p == ' ') {
      ++p;
    }
    const long seconds = *p ? strtol(p, nullptr, 10) : kDefaultPairSeconds;
    pairing_begin(seconds > 0 ? static_cast<uint32_t>(seconds) : kDefaultPairSeconds);
    Serial.printf("remote: pairing code=%s seconds=%lu endpoint=/pair\n", s_pair_code,
                  static_cast<unsigned long>((s_pair_until_ms - millis()) / 1000u));
    return true;
  }
  if (strcmp(line, "remote clear") == 0 || strcmp(line, "remote unpair") == 0) {
    remote_save_key("");
    pairing_clear();
    Serial.println("remote: cleared paired key");
    return true;
  }
  return false;
}

bool pm_remote_control_enqueue(const PmRemoteCommand &cmd) {
  if (s_count >= static_cast<uint8_t>(sizeof(s_queue) / sizeof(s_queue[0]))) {
    return false;
  }
  PmRemoteCommand copy = cmd;
  copy.seq = ++s_seq;
  s_queue[s_write] = copy;
  s_write = static_cast<uint8_t>((s_write + 1) % (sizeof(s_queue) / sizeof(s_queue[0])));
  ++s_count;
  return true;
}

bool pm_remote_control_take(PmRemoteCommand *out) {
  if (!out || s_count == 0) {
    return false;
  }
  *out = s_queue[s_read];
  s_read = static_cast<uint8_t>((s_read + 1) % (sizeof(s_queue) / sizeof(s_queue[0])));
  --s_count;
  return true;
}

void pm_remote_control_register_http(WebServer &server) {
  const char *headers[] = {"Authorization", "X-Astrolabe-Key"};
  server.collectHeaders(headers, 2);

  server.on("/control", HTTP_GET, [&server]() {
    remote_load_key();
    const uint32_t remain = pairing_active() ? (s_pair_until_ms - millis()) / 1000u : 0u;
    char body[224];
    snprintf(body, sizeof(body),
             "{\"enabled\":%s,\"paired\":%s,\"pairing\":%s,\"pairingSeconds\":%lu,\"queued\":%u,\"seq\":%lu}\n",
             pm_remote_control_enabled() ? "true" : "false", s_paired_key[0] ? "true" : "false",
             pairing_active() ? "true" : "false", static_cast<unsigned long>(remain), static_cast<unsigned>(s_count),
             static_cast<unsigned long>(s_seq));
    send_json(server, 200, body);
  });

  server.on("/pair", HTTP_POST, [&server]() {
    if (!pairing_active()) {
      send_json(server, 403, "{\"ok\":false,\"error\":\"pairing closed\"}\n");
      return;
    }
    const String body_s = server.arg("plain");
    if (body_s.length() == 0 || body_s.length() > 512) {
      send_json(server, 400, "{\"ok\":false,\"error\":\"bad body\"}\n");
      return;
    }
    JsonDocument doc;
    if (deserializeJson(doc, body_s.c_str())) {
      send_json(server, 400, "{\"ok\":false,\"error\":\"bad json\"}\n");
      return;
    }
    const char *code = doc["code"] | "";
    if (!secure_equals(code, s_pair_code)) {
      send_json(server, 401, "{\"ok\":false,\"error\":\"bad code\"}\n");
      return;
    }
    char token[65];
    random_hex(token, sizeof(token), 32);
    remote_save_key(token);
    pairing_clear();
    char resp[128];
    snprintf(resp, sizeof(resp), "{\"ok\":true,\"token\":\"%s\"}\n", token);
    send_json(server, 200, resp);
  });

  server.on("/control", HTTP_POST, [&server]() {
    if (!pm_remote_control_enabled()) {
      send_json(server, 403, "{\"ok\":false,\"error\":\"remote control disabled\"}\n");
      return;
    }
    if (!http_authorized(server)) {
      send_json(server, 401, "{\"ok\":false,\"error\":\"unauthorized\"}\n");
      return;
    }
    const String body_s = server.arg("plain");
    if (body_s.length() == 0 || body_s.length() > kRemoteJsonCap) {
      send_json(server, 400, "{\"ok\":false,\"error\":\"bad body\"}\n");
      return;
    }
    PmRemoteCommand cmd;
    char err[40] = "";
    if (!parse_command_json(body_s.c_str(), &cmd, err, sizeof(err))) {
      char resp[96];
      snprintf(resp, sizeof(resp), "{\"ok\":false,\"error\":\"%s\"}\n", err);
      send_json(server, 400, resp);
      return;
    }
    if (!pm_remote_control_enqueue(cmd)) {
      send_json(server, 429, "{\"ok\":false,\"error\":\"queue full\"}\n");
      return;
    }
    char resp[96];
    snprintf(resp, sizeof(resp), "{\"ok\":true,\"seq\":%lu}\n", static_cast<unsigned long>(s_seq));
    send_json(server, 202, resp);
  });
}

#if PM_REMOTE_BLE
namespace {

class RemoteCommandCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *characteristic) override {
    if (!characteristic) {
      return;
    }
    const std::string raw = characteristic->getValue();
    if (raw.empty() || raw.size() > kRemoteJsonCap) {
      characteristic->setValue("{\"ok\":false,\"error\":\"bad body\"}\n");
      return;
    }
    JsonDocument doc;
    if (deserializeJson(doc, raw.data(), raw.size())) {
      characteristic->setValue("{\"ok\":false,\"error\":\"bad json\"}\n");
      return;
    }
    const char *token = doc["token"] | "";
    if (!pm_remote_control_enabled() || !auth_value_ok(token)) {
      characteristic->setValue("{\"ok\":false,\"error\":\"unauthorized\"}\n");
      return;
    }
    PmRemoteCommand cmd;
    char err[40] = "";
    if (!parse_command_json(raw.c_str(), &cmd, err, sizeof(err))) {
      char resp[96];
      snprintf(resp, sizeof(resp), "{\"ok\":false,\"error\":\"%s\"}\n", err);
      characteristic->setValue(resp);
      return;
    }
    if (!pm_remote_control_enqueue(cmd)) {
      characteristic->setValue("{\"ok\":false,\"error\":\"queue full\"}\n");
      return;
    }
    char resp[96];
    snprintf(resp, sizeof(resp), "{\"ok\":true,\"seq\":%lu}\n", static_cast<unsigned long>(s_seq));
    characteristic->setValue(resp);
  }
};

RemoteCommandCallbacks s_ble_command_callbacks;

}  // namespace
#endif

void pm_remote_control_attach_ble(BLEServer *server) {
#if PM_REMOTE_BLE
  if (!server) {
    return;
  }
  BLEService *service = server->createService(kRemoteServiceUuid);
  BLECharacteristic *command = service->createCharacteristic(
      kRemoteCommandUuid, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_READ);
  command->setCallbacks(&s_ble_command_callbacks);
  command->setValue(pm_remote_control_enabled() ? "{\"enabled\":true}\n" : "{\"enabled\":false}\n");
  service->start();
  pm_log_printf(false, "remote: BLE control service attached enabled=%d", pm_remote_control_enabled() ? 1 : 0);
#else
  (void)server;
#endif
}
