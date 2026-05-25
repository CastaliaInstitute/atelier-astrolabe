#include <nlohmann/json.hpp>

#include <atomic>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "AstrolabeSpeakerAudioSink.h"
#include "BellTask.h"
#include "BellUtils.h"
#include "CSpotContext.h"
#include "CircularBuffer.h"
#include "Logger.h"
#include "LoginBlob.h"
#include "SpircHandler.h"
#include "TrackPlayer.h"
#include "civetweb.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_spiffs.h"
#include "esp_wifi.h"
#include "freertos/event_groups.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mdns.h"
#include "nvs.h"
#include "nvs_flash.h"

#ifndef ASTROLABE_CSPOT_DEVICE_NAME
#define ASTROLABE_CSPOT_DEVICE_NAME "Astrolabe"
#endif
#ifndef ASTROLABE_WIFI_DEFAULT_SSID
#define ASTROLABE_WIFI_DEFAULT_SSID "The Chateau"
#endif
#ifndef ASTROLABE_WIFI_DEFAULT_PASS
#define ASTROLABE_WIFI_DEFAULT_PASS "thechateau"
#endif

static const char *TAG = "astrolabe_cspot";
static constexpr const char *kAuthBlobPath = "/spiffs/cspot-auth.json";
static constexpr uint32_t kPlayerBufferBytes = 1024u * 128u * 4u;
static EventGroupHandle_t s_wifiEvents;
static constexpr EventBits_t kWifiConnectedBit = BIT0;
static constexpr EventBits_t kWifiFailedBit = BIT1;

class AstrolabeCspotPlayer : public bell::Task {
 public:
  explicit AstrolabeCspotPlayer(std::shared_ptr<cspot::SpircHandler> handler)
      : bell::Task("cspot_player", 8 * 1024, 0, 0), handler_(std::move(handler)) {
    sink_ = std::make_unique<AstrolabeSpeakerAudioSink>();
    sink_->setParams(44100, 2, 16);
    sink_->volumeChanged(70);
    buffer_ = std::make_unique<bell::CircularBuffer>(kPlayerBufferBytes);
    handler_->getTrackPlayer()->setDataCallback(
        [this](uint8_t *data, size_t bytes, std::string_view) { return feedData(data, bytes); });
    handler_->setEventHandler([this](std::unique_ptr<cspot::SpircHandler::Event> event) {
      switch (event->eventType) {
        case cspot::SpircHandler::EventType::PLAY_PAUSE:
          paused_ = std::get<bool>(event->data);
          break;
        case cspot::SpircHandler::EventType::FLUSH:
        case cspot::SpircHandler::EventType::SEEK:
        case cspot::SpircHandler::EventType::PLAYBACK_START:
          buffer_->emptyBuffer();
          break;
        default:
          break;
      }
    });
    startTask();
  }

  size_t feedData(uint8_t *data, size_t len) {
    size_t remaining = len;
    while (remaining > 0) {
      const size_t offset = len - remaining;
      const size_t written = buffer_->write(data + offset, remaining);
      if (written == 0) {
        BELL_SLEEP_MS(10);
        continue;
      }
      remaining -= written;
    }
    return len;
  }

  void runTask() override {
    std::vector<uint8_t> out(2048);
    while (true) {
      if (paused_) {
        BELL_SLEEP_MS(50);
        continue;
      }
      const size_t read = buffer_->read(out.data(), out.size());
      if (read == 0) {
        BELL_SLEEP_MS(25);
        continue;
      }
      sink_->feedPCMFrames(out.data(), read);
    }
  }

 private:
  std::shared_ptr<cspot::SpircHandler> handler_;
  std::unique_ptr<AstrolabeSpeakerAudioSink> sink_;
  std::unique_ptr<bell::CircularBuffer> buffer_;
  std::atomic<bool> paused_ = false;
};

static bool readFile(const char *path, std::string *out) {
  std::ifstream file(path);
  if (!file.good()) {
    return false;
  }
  out->assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
  return !out->empty();
}

static bool writeFile(const char *path, const std::string &body) {
  std::ofstream file(path, std::ios::trunc);
  if (!file.good()) {
    return false;
  }
  file << body;
  return file.good();
}

static esp_err_t initSpiffs() {
  esp_vfs_spiffs_conf_t conf = {
      .base_path = "/spiffs",
      .partition_label = nullptr,
      .max_files = 4,
      .format_if_mount_failed = true,
  };
  esp_err_t err = esp_vfs_spiffs_register(&conf);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    return err;
  }
  size_t total = 0;
  size_t used = 0;
  err = esp_spiffs_info(conf.partition_label, &total, &used);
  if (err == ESP_OK) {
    ESP_LOGI(TAG, "spiffs total=%u used=%u", (unsigned)total, (unsigned)used);
  }
  return ESP_OK;
}

static bool nvsGetString(nvs_handle_t nvs, const char *key, char *out, size_t outSize) {
  if (!out || outSize == 0) {
    return false;
  }
  out[0] = '\0';
  size_t len = outSize;
  const esp_err_t err = nvs_get_str(nvs, key, out, &len);
  return err == ESP_OK && out[0] != '\0';
}

static void loadWifiCredentials(char *ssid, size_t ssidSize, char *pass, size_t passSize) {
  strlcpy(ssid, ASTROLABE_WIFI_DEFAULT_SSID, ssidSize);
  strlcpy(pass, ASTROLABE_WIFI_DEFAULT_PASS, passSize);

  nvs_handle_t nvs = 0;
  if (nvs_open("mynah", NVS_READONLY, &nvs) == ESP_OK) {
    (void)nvsGetString(nvs, "ssid", ssid, ssidSize);
    (void)nvsGetString(nvs, "pass", pass, passSize);
    nvs_close(nvs);
  }
}

static void wifiEventHandler(void *, esp_event_base_t eventBase, int32_t eventId, void *) {
  if (eventBase == WIFI_EVENT && eventId == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (eventBase == WIFI_EVENT && eventId == WIFI_EVENT_STA_DISCONNECTED) {
    esp_wifi_connect();
  } else if (eventBase == IP_EVENT && eventId == IP_EVENT_STA_GOT_IP) {
    xEventGroupSetBits(s_wifiEvents, kWifiConnectedBit);
  }
}

static esp_err_t connectWifiFromAstrolabeNvs() {
  char ssid[64];
  char pass[64];
  loadWifiCredentials(ssid, sizeof(ssid), pass, sizeof(pass));
  ESP_RETURN_ON_FALSE(ssid[0] != '\0', ESP_ERR_INVALID_STATE, TAG, "missing wifi ssid");

  s_wifiEvents = xEventGroupCreate();
  ESP_RETURN_ON_FALSE(s_wifiEvents, ESP_ERR_NO_MEM, TAG, "wifi event group");
  esp_netif_create_default_wifi_sta();

  wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
  ESP_RETURN_ON_ERROR(esp_wifi_init(&init), TAG, "wifi init");
  ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifiEventHandler, nullptr), TAG,
                      "wifi handler");
  ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifiEventHandler, nullptr), TAG,
                      "ip handler");

  wifi_config_t cfg = {};
  strlcpy(reinterpret_cast<char *>(cfg.sta.ssid), ssid, sizeof(cfg.sta.ssid));
  strlcpy(reinterpret_cast<char *>(cfg.sta.password), pass, sizeof(cfg.sta.password));
  cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
  cfg.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;

  ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "wifi mode");
  ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &cfg), TAG, "wifi config");
  ESP_RETURN_ON_ERROR(esp_wifi_set_ps(WIFI_PS_NONE), TAG, "wifi ps");
  ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "wifi start");

  ESP_LOGI(TAG, "connecting wifi ssid=%s", ssid);
  const EventBits_t bits =
      xEventGroupWaitBits(s_wifiEvents, kWifiConnectedBit | kWifiFailedBit, pdFALSE, pdFALSE, pdMS_TO_TICKS(30000));
  return (bits & kWifiConnectedBit) ? ESP_OK : ESP_ERR_TIMEOUT;
}

struct ZeroconfHttpContext {
  std::shared_ptr<cspot::LoginBlob> blob;
  std::atomic<bool> *gotBlob = nullptr;
};

static int hexValue(char c) {
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

static std::string formDecode(const std::string &value) {
  std::string out;
  out.reserve(value.size());
  for (size_t i = 0; i < value.size(); ++i) {
    if (value[i] == '+') {
      out.push_back(' ');
    } else if (value[i] == '%' && i + 2 < value.size()) {
      const int hi = hexValue(value[i + 1]);
      const int lo = hexValue(value[i + 2]);
      if (hi >= 0 && lo >= 0) {
        out.push_back(static_cast<char>((hi << 4) | lo));
        i += 2;
      } else {
        out.push_back(value[i]);
      }
    } else {
      out.push_back(value[i]);
    }
  }
  return out;
}

static std::map<std::string, std::string> parseFormBody(const std::string &body) {
  std::map<std::string, std::string> params;
  size_t pos = 0;
  while (pos <= body.size()) {
    const size_t amp = body.find('&', pos);
    const size_t end = amp == std::string::npos ? body.size() : amp;
    const size_t eq = body.find('=', pos);
    if (eq != std::string::npos && eq < end) {
      params[formDecode(body.substr(pos, eq - pos))] = formDecode(body.substr(eq + 1, end - eq - 1));
    }
    if (amp == std::string::npos) {
      break;
    }
    pos = amp + 1;
  }
  return params;
}

static void sendJson(httpd_req_t *req, const std::string &body) {
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_send(req, body.c_str(), body.size());
}

static esp_err_t zeroconfGetHandler(httpd_req_t *req) {
  auto *ctx = static_cast<ZeroconfHttpContext *>(req->user_ctx);
  ESP_LOGI(TAG, "spotify_info GET from socket=%d", httpd_req_to_sockfd(req));
  sendJson(req, ctx->blob->buildZeroconfInfo());
  return ESP_OK;
}

static esp_err_t zeroconfPostHandler(httpd_req_t *req) {
  auto *ctx = static_cast<ZeroconfHttpContext *>(req->user_ctx);
  std::string body;
  body.resize(req->content_len);
  size_t received = 0;
  while (received < body.size()) {
    const int read = httpd_req_recv(req, body.data() + received, body.size() - received);
    if (read <= 0) {
      return ESP_FAIL;
    }
    received += read;
  }

  auto params = parseFormBody(body);
  ESP_LOGI(TAG, "spotify_info POST from socket=%d bytes=%u", httpd_req_to_sockfd(req), (unsigned)body.size());
  ctx->blob->loadZeroconfQuery(params);
  *ctx->gotBlob = true;

  nlohmann::json reply;
  reply["status"] = 101;
  reply["spotifyError"] = 0;
  reply["statusString"] = "ERROR-OK";
  sendJson(req, reply.dump());
  return ESP_OK;
}

static esp_err_t startZeroconfHttpServer(ZeroconfHttpContext *ctx, httpd_handle_t *server) {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 8080;
  config.ctrl_port = 8081;
  config.stack_size = 8192;
  ESP_RETURN_ON_ERROR(httpd_start(server, &config), TAG, "http server");

  httpd_uri_t get = {
      .uri = "/spotify_info",
      .method = HTTP_GET,
      .handler = zeroconfGetHandler,
      .user_ctx = ctx,
  };
  httpd_uri_t post = {
      .uri = "/spotify_info",
      .method = HTTP_POST,
      .handler = zeroconfPostHandler,
      .user_ctx = ctx,
  };
  ESP_RETURN_ON_ERROR(httpd_register_uri_handler(*server, &get), TAG, "http get");
  ESP_RETURN_ON_ERROR(httpd_register_uri_handler(*server, &post), TAG, "http post");
  ESP_LOGI(TAG, "zeroconf HTTP server listening on port 8080");
  return ESP_OK;
}

static std::shared_ptr<cspot::LoginBlob> waitForZeroconfLogin(const std::string &deviceName) {
  std::atomic<bool> gotBlob = false;
  auto blob = std::make_shared<cspot::LoginBlob>(deviceName);
  ZeroconfHttpContext httpCtx{blob, &gotBlob};
  httpd_handle_t server = nullptr;
  ESP_LOGI(TAG, "starting zeroconf HTTP pairing endpoint");
  ESP_ERROR_CHECK(startZeroconfHttpServer(&httpCtx, &server));

  mdns_txt_item_t txt[] = {{"VERSION", "1.0"}, {"CPath", "/spotify_info"}, {"Stack", "SP"}};
  ESP_LOGI(TAG, "advertising Spotify Connect mDNS service as '%s'", blob->getDeviceName().c_str());
  ESP_ERROR_CHECK(
      mdns_service_add(blob->getDeviceName().c_str(), "_spotify-connect", "_tcp", 8080, txt, sizeof(txt) / sizeof(txt[0])));
  ESP_LOGI(TAG, "waiting for Spotify app to connect to '%s'", deviceName.c_str());
  uint32_t waitedSeconds = 0;
  while (!gotBlob) {
    waitedSeconds += 5;
    ESP_LOGI(TAG, "Spotify pairing endpoint active for %us", (unsigned)waitedSeconds);
    BELL_SLEEP_MS(5000);
  }
  httpd_stop(server);
  return blob;
}

class CspotReceiverTask : public bell::Task {
 public:
  CspotReceiverTask() : bell::Task("cspot_rx", 32 * 1024, 0, 1) { startTask(); }

  void runTask() override {
    const std::string deviceName = ASTROLABE_CSPOT_DEVICE_NAME;
    ESP_ERROR_CHECK(mdns_init());
    mdns_hostname_set("astrolabe-cspot");

    auto blob = std::make_shared<cspot::LoginBlob>(deviceName);
    std::string cached;
    if (readFile(kAuthBlobPath, &cached)) {
      ESP_LOGI(TAG, "loading cached Spotify credentials");
      blob->loadJson(cached);
    } else {
      blob = waitForZeroconfLogin(deviceName);
    }

    auto ctx = cspot::Context::createFromBlob(blob);
    ctx->session->connectWithRandomAp();
    ctx->config.authData = ctx->session->authenticate(blob);
    if (ctx->config.authData.empty()) {
      ESP_LOGE(TAG, "Spotify authentication failed");
      vTaskDelay(portMAX_DELAY);
    }

    const std::string persisted = ctx->getCredentialsJson();
    if (writeFile(kAuthBlobPath, persisted)) {
      ESP_LOGI(TAG, "stored Spotify credentials");
    }

    ctx->session->startTask();
    auto handler = std::make_shared<cspot::SpircHandler>(ctx);
    handler->subscribeToMercury();
    auto player = std::make_shared<AstrolabeCspotPlayer>(handler);
    ESP_LOGI(TAG, "Spotify Connect receiver ready heap=%u psram=%u", (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));

    while (true) {
      ctx->session->handlePacket();
    }
  }
};

extern "C" void app_main(void) {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);
  ESP_ERROR_CHECK(initSpiffs());

  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  ESP_ERROR_CHECK(connectWifiFromAstrolabeNvs());

  bell::setDefaultLogger();
  ESP_LOGI(TAG, "starting native Astrolabe Spotify Connect receiver");
  static auto task = std::make_unique<CspotReceiverTask>();
  (void)task;
}
