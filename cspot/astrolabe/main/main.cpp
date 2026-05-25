#include <nlohmann/json.hpp>

#include <atomic>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "AstrolabeSpeakerAudioSink.h"
#include "AstrolabeAudioVisualizer.h"
#include "BellTask.h"
#include "BellUtils.h"
#include "CSpotContext.h"
#include "CircularBuffer.h"
#include "bell/main/io/include/HTTPClient.h"
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
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "mdns.h"
#include "nvs.h"
#include "nvs_flash.h"

#if defined(ASTROLABE_WAVESHARE_S3_185)
#include <Arduino.h>
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include <JPEGDEC.h>
#include <pgmspace.h>
#include <Wire.h>
#include <databus/Arduino_ESP32QSPI.h>
#include <display/Arduino_ST77916.h>
#include <font/glcdfont.h>

#include "pin_config.h"
#include "pm_mic.h"
#endif

#ifndef ASTROLABE_CSPOT_DEVICE_NAME
#define ASTROLABE_CSPOT_DEVICE_NAME "Astrolabe"
#endif
#ifndef ASTROLABE_WIFI_DEFAULT_SSID
#define ASTROLABE_WIFI_DEFAULT_SSID "The Chateau"
#endif
#ifndef ASTROLABE_WIFI_DEFAULT_PASS
#define ASTROLABE_WIFI_DEFAULT_PASS "thechateau"
#endif
#ifndef ASTROLABE_CSPOT_BUTTON_PIN
#define ASTROLABE_CSPOT_BUTTON_PIN 0
#endif

static const char *TAG = "astrolabe_cspot";
static constexpr const char *kAuthBlobPath = "/spiffs/cspot-auth.json";
static constexpr uint32_t kPlayerBufferBytes = 1024u * 128u * 4u;
static EventGroupHandle_t s_wifiEvents;
static constexpr EventBits_t kWifiConnectedBit = BIT0;
static constexpr EventBits_t kWifiFailedBit = BIT1;
static std::string s_astrolabeDeviceName = ASTROLABE_CSPOT_DEVICE_NAME;
static std::string s_astrolabeMdnsHost = "astrolabe-cspot";

static const char *astrolabeDeviceName() {
  return s_astrolabeDeviceName.c_str();
}

static const char *astrolabeMdnsHost() {
  return s_astrolabeMdnsHost.c_str();
}

#if defined(ASTROLABE_WAVESHARE_S3_185)
static SemaphoreHandle_t s_displayMutex = nullptr;
static Arduino_DataBus *s_displayBus = nullptr;
static Arduino_GFX *s_panel = nullptr;
static Arduino_GFX *s_display = nullptr;
static constexpr int kAlbumArtSize = 96;
static constexpr size_t kAlbumArtMaxBytes = 150000;
static constexpr uint8_t kCst816Addr = 0x15;
static constexpr uint8_t kCst816RegGesture = 0x01;
static constexpr int kVinylRadius = (LCD_WIDTH < LCD_HEIGHT ? LCD_WIDTH : LCD_HEIGHT) / 2 - 8;
static constexpr int kVinylLabelRadius = kVinylRadius / 2;
static constexpr int kVinylHoleRadius = 7;
static constexpr int kVinylLabelFrame = (kVinylLabelRadius + 2) * 2 + 1;
static constexpr int kVinylBezelOuterRadius = (LCD_WIDTH < LCD_HEIGHT ? LCD_WIDTH : LCD_HEIGHT) / 2 - 1;
static constexpr int kVinylBezelInnerRadius = kVinylBezelOuterRadius - 42;

enum class DisplayFace : uint8_t {
  Vinyl = 0,
  Visualizer = 1,
  Instrument = 2,
};

struct PlayerFaceState {
  char state[24] = "Starting";
  char title[64] = "Astrolabe";
  char artist[48] = "Spotify Connect";
  char album[48] = "";
  char artUrl[192] = "";
  uint16_t primary = 0xffff;
  uint16_t accent = 0x07ef;
  bool playing = false;
  uint32_t progressMs = 0;
  uint32_t durationMs = 210000;
  uint32_t lastTickMs = 0;
};

struct AlbumDecodeState {
  uint16_t *fb = nullptr;
  int cropX = 0;
  int cropY = 0;
  int cropSide = 0;
};

static PlayerFaceState s_face;
static SemaphoreHandle_t s_faceMutex = nullptr;
static SemaphoreHandle_t s_albumArtMutex = nullptr;
static uint16_t *s_albumArtFb = nullptr;
static uint16_t s_vinylLabelFb[kVinylLabelFrame * kVinylLabelFrame];
static char s_albumArtUrl[192] = "";
static bool s_albumArtReady = false;
static bool s_albumArtFetching = false;
static bool s_albumArtDrawLogged = false;
static AlbumDecodeState s_albumDecode;
static DisplayFace s_displayFace = DisplayFace::Vinyl;
static uint8_t s_visualizerMode = 0;
static uint8_t s_instrumentMode = 0;
static int s_lastInstrumentNote = -1;
static uint32_t s_lastInstrumentNoteMs = 0;
static std::shared_ptr<cspot::SpircHandler> s_transportHandler;
static std::atomic<bool> s_spotifyPaused{true};
static std::atomic<bool> s_spotifyActiveHere{false};
static bool s_visualizerNeedsClear = true;
static bool s_vinylFaceDrawn = false;
static bool s_vinylDirty = true;
static bool s_vinylHasArtworkFrame = false;
static uint32_t s_lastVinylTextFrameMs = 0;
static char s_vinylRenderedKey[320] = "";

static void markVinylDirty(const char *reason) {
  s_vinylDirty = true;
  ESP_LOGI(TAG, "vinyl dirty: %s", reason ? reason : "unknown");
}

struct StaffNote {
  uint32_t bornMs = 0;
  int y = 0;
  uint16_t color = 0xffff;
  float velocity = 0.f;
  bool sharp = false;
};

static StaffNote s_staffNotes[14];
static uint8_t s_staffNoteHead = 0;
static uint32_t s_lastStaffNoteMs = 0;
static int s_lastStaffBand = -1;
static float s_smoothBands[ASTROLABE_AUDIO_VIS_BANDS] = {};
static float s_smoothWave[ASTROLABE_AUDIO_VIS_WAVE_POINTS] = {};
static float s_smoothLevel = 0.f;

static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return static_cast<uint16_t>(((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3));
}

static uint16_t hashColor(const char *text, uint8_t variant) {
  uint32_t h = 2166136261u ^ variant;
  for (const char *p = text; p && *p; ++p) {
    h ^= static_cast<uint8_t>(*p);
    h *= 16777619u;
  }
  return rgb565(80 + (h & 0x7f), 54 + ((h >> 8) & 0x5f), 64 + ((h >> 16) & 0x5f));
}

static void copyTrunc(char *dst, size_t cap, const char *src) {
  if (!dst || cap == 0) {
    return;
  }
  if (!src) {
    dst[0] = '\0';
    return;
  }
  strncpy(dst, src, cap - 1);
  dst[cap - 1] = '\0';
}

static void copyTrunc(char *dst, size_t cap, const std::string &src) {
  copyTrunc(dst, cap, src.c_str());
}

static bool takeMutex(SemaphoreHandle_t *mux, uint32_t timeoutMs) {
  if (!*mux) {
    *mux = xSemaphoreCreateMutex();
  }
  return *mux && xSemaphoreTake(*mux, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
}

static void giveMutex(SemaphoreHandle_t mux) {
  if (mux) {
    xSemaphoreGive(mux);
  }
}

static void drawCentered(const char *text, int y, uint16_t color, uint8_t size = 1) {
  if (!s_display || !text) {
    return;
  }
  const int16_t w = static_cast<int16_t>(strlen(text) * 6 * size);
  s_display->setTextWrap(false);
  s_display->setTextColor(color, 0x0000);
  s_display->setTextSize(size);
  s_display->setCursor(std::max<int16_t>(0, (LCD_WIDTH - w) / 2), y);
  s_display->print(text);
}

static void clippedText(char *out, size_t cap, const char *src, size_t maxChars) {
  if (!out || cap == 0) {
    return;
  }
  if (!src || !src[0]) {
    out[0] = '\0';
    return;
  }
  const size_t n = std::min(maxChars, cap - 1);
  strncpy(out, src, n);
  out[n] = '\0';
  if (strlen(src) > n && n > 2) {
    out[n - 1] = '.';
  }
}

static const char *jpegErrorName(int err) {
  switch (err) {
  case JPEG_SUCCESS:
    return "success";
  case JPEG_INVALID_PARAMETER:
    return "invalid-parameter";
  case JPEG_DECODE_ERROR:
    return "decode-error";
  case JPEG_UNSUPPORTED_FEATURE:
    return "unsupported-feature";
  case JPEG_INVALID_FILE:
    return "invalid-file";
  default:
    return "unknown";
  }
}

static const char *jpegTypeName(int type) {
  switch (type) {
  case JPEG_MODE_BASELINE:
    return "baseline";
  case JPEG_MODE_PROGRESSIVE:
    return "progressive";
  case JPEG_MODE_INVALID:
    return "invalid";
  default:
    return "unknown";
  }
}

static int albumJpegDraw(JPEGDRAW *pDraw) {
  if (!pDraw || !s_albumDecode.fb || s_albumDecode.cropSide <= 0) {
    return 0;
  }
  for (int y = 0; y < pDraw->iHeight; ++y) {
    const int sy = pDraw->y + y;
    if (sy < s_albumDecode.cropY || sy >= s_albumDecode.cropY + s_albumDecode.cropSide) {
      continue;
    }
    const int dy = (sy - s_albumDecode.cropY) * kAlbumArtSize / s_albumDecode.cropSide;
    for (int x = 0; x < pDraw->iWidth; ++x) {
      const int sx = pDraw->x + x;
      if (sx < s_albumDecode.cropX || sx >= s_albumDecode.cropX + s_albumDecode.cropSide) {
        continue;
      }
      const int dx = (sx - s_albumDecode.cropX) * kAlbumArtSize / s_albumDecode.cropSide;
      if (dx >= 0 && dx < kAlbumArtSize && dy >= 0 && dy < kAlbumArtSize) {
        s_albumDecode.fb[dy * kAlbumArtSize + dx] = pDraw->pPixels[y * pDraw->iWidth + x];
      }
    }
  }
  return 1;
}

static bool downloadHttps(const char *url, uint8_t **outBytes, size_t *outLen) {
  if (!outBytes || !outLen) {
    return false;
  }
  *outBytes = nullptr;
  *outLen = 0;

  esp_http_client_config_t config = {};
  config.url = url;
  config.crt_bundle_attach = esp_crt_bundle_attach;
  config.timeout_ms = 9000;
  config.buffer_size = 1024;
  config.buffer_size_tx = 1024;
  config.keep_alive_enable = false;
  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client) {
    return false;
  }

  esp_http_client_set_header(client, "Accept", "image/jpeg");
  esp_err_t err = esp_http_client_open(client, 0);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "album art open failed: %s", esp_err_to_name(err));
    esp_http_client_cleanup(client);
    return false;
  }
  int contentLength = esp_http_client_fetch_headers(client);
  const int status = esp_http_client_get_status_code(client);
  if (status < 200 || status >= 300 || static_cast<size_t>(std::max(contentLength, 0)) > kAlbumArtMaxBytes) {
    ESP_LOGW(TAG, "album art invalid status=%d length=%d", status, contentLength);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return false;
  }
  const size_t initialCap = contentLength > 0 ? static_cast<size_t>(contentLength) : 24 * 1024;
  uint8_t *buf = static_cast<uint8_t *>(heap_caps_malloc(initialCap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!buf) {
    ESP_LOGW(TAG, "album art alloc failed len=%u", (unsigned)initialCap);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return false;
  }
  size_t cap = initialCap;
  size_t read = 0;
  while (read < kAlbumArtMaxBytes) {
    if (read == cap) {
      const size_t nextCap = std::min(kAlbumArtMaxBytes, cap * 2);
      uint8_t *next = static_cast<uint8_t *>(heap_caps_realloc(buf, nextCap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
      if (!next) {
        ESP_LOGW(TAG, "album art realloc failed cap=%u", (unsigned)nextCap);
        break;
      }
      buf = next;
      cap = nextCap;
    }
    const int n = esp_http_client_read(client, reinterpret_cast<char *>(buf + read), static_cast<int>(cap - read));
    if (n > 0) {
      read += static_cast<size_t>(n);
      if (contentLength > 0 && read >= static_cast<size_t>(contentLength)) {
        break;
      }
      continue;
    }
    if (n < 0) {
      break;
    }
    if (esp_http_client_is_complete_data_received(client)) {
      break;
    }
    delay(1);
  }
  esp_http_client_close(client);
  esp_http_client_cleanup(client);
  if (read < 8) {
    ESP_LOGW(TAG, "album art short read status=%d length=%d read=%u", status, contentLength, (unsigned)read);
    free(buf);
    return false;
  }
  ESP_LOGI(TAG, "album art downloaded status=%d length=%d read=%u magic=%02x %02x %02x %02x", status, contentLength,
           (unsigned)read, buf[0], buf[1], buf[2], buf[3]);
  *outBytes = buf;
  *outLen = read;
  return true;
}

static bool decodeAlbumJpeg(const uint8_t *data, size_t len) {
  uint8_t *decodeBytes = static_cast<uint8_t *>(heap_caps_malloc(len, MALLOC_CAP_8BIT));
  if (!decodeBytes) {
    ESP_LOGW(TAG, "album art internal decode buffer alloc failed len=%u", (unsigned)len);
    return false;
  }
  memcpy(decodeBytes, data, len);
  uint16_t *fb = static_cast<uint16_t *>(
      heap_caps_calloc(kAlbumArtSize * kAlbumArtSize, sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!fb) {
    free(decodeBytes);
    return false;
  }
  JPEGDEC jpg;
  s_albumDecode = {};
  s_albumDecode.fb = fb;
  if (jpg.openRAM(decodeBytes, static_cast<int>(len), albumJpegDraw) != 1) {
    const int err = jpg.getLastError();
    ESP_LOGW(TAG, "album art jpeg open failed len=%u err=%s(%d)", (unsigned)len, jpegErrorName(err), err);
    free(fb);
    free(decodeBytes);
    return false;
  }
  const int w = jpg.getWidth();
  const int h = jpg.getHeight();
  const int type = jpg.getJPEGType();
  ESP_LOGI(TAG, "album art jpeg header %dx%d type=%s(%d) thumb=%d", w, h, jpegTypeName(type), type, jpg.hasThumb());
  int decodeOptions = 0;
  if (type == JPEG_MODE_PROGRESSIVE) {
    decodeOptions = JPEG_SCALE_EIGHTH;
  }
  const int scaleDiv = (decodeOptions & JPEG_SCALE_EIGHTH) ? 8 : 1;
  const int outW = (w + scaleDiv - 1) / scaleDiv;
  const int outH = (h + scaleDiv - 1) / scaleDiv;
  s_albumDecode.cropSide = std::min(outW, outH);
  s_albumDecode.cropX = (outW - s_albumDecode.cropSide) / 2;
  s_albumDecode.cropY = (outH - s_albumDecode.cropSide) / 2;
  jpg.setPixelType(RGB565_LITTLE_ENDIAN);
  const bool ok = jpg.decode(0, 0, decodeOptions) == 1;
  const int decodeErr = jpg.getLastError();
  jpg.close();
  free(decodeBytes);
  s_albumDecode = {};
  if (!ok) {
    ESP_LOGW(TAG, "album art jpeg decode failed %dx%d type=%s(%d) err=%s(%d) len=%u", w, h, jpegTypeName(type), type,
             jpegErrorName(decodeErr), decodeErr, (unsigned)len);
    free(fb);
    return false;
  }
  if (!takeMutex(&s_albumArtMutex, 250)) {
    free(fb);
    return false;
  }
  if (s_albumArtFb) {
    free(s_albumArtFb);
  }
  s_albumArtFb = fb;
  s_albumArtReady = true;
  s_albumArtDrawLogged = false;
  giveMutex(s_albumArtMutex);
  ESP_LOGI(TAG, "album art decoded %dx%d", w, h);
  return true;
}

static bool tca9554Write(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(MYNAH_TCA9554_ADDR);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

static bool tca9554Read(uint8_t reg, uint8_t *value) {
  if (!value) {
    return false;
  }
  Wire.beginTransmission(MYNAH_TCA9554_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission() != 0) {
    return false;
  }
  if (Wire.requestFrom(MYNAH_TCA9554_ADDR, 1) != 1) {
    return false;
  }
  *value = Wire.read();
  return true;
}

static bool cst816Read(uint8_t reg, uint8_t *data, uint8_t len) {
  if (!data || len == 0) {
    return false;
  }
  Wire.beginTransmission(kCst816Addr);
  Wire.write(reg);
  if (Wire.endTransmission(true) != 0) {
    return false;
  }
  if (Wire.requestFrom(kCst816Addr, len) != len) {
    return false;
  }
  for (uint8_t i = 0; i < len; ++i) {
    data[i] = Wire.read();
  }
  return true;
}

static void tca9554SetPin(uint8_t pin, bool high) {
  uint8_t out = 0;
  (void)tca9554Read(0x01, &out);
  const uint8_t mask = static_cast<uint8_t>(1u << (pin - 1u));
  out = high ? static_cast<uint8_t>(out | mask) : static_cast<uint8_t>(out & ~mask);
  (void)tca9554Write(0x01, out);
}

static void prepareWaveshareDisplayPower() {
  Wire.end();
  delay(2);
  Wire.setPins(IIC_SDA, IIC_SCL);
  Wire.begin(IIC_SDA, IIC_SCL, 400000);
  (void)tca9554Write(0x03, 0x00);
  tca9554SetPin(MYNAH_EXIO_TOUCH_RST, true);
  tca9554SetPin(MYNAH_EXIO_LCD_RST, false);
  delay(10);
  tca9554SetPin(MYNAH_EXIO_LCD_RST, true);
  delay(120);
  pinMode(LCD_BL, OUTPUT);
  analogWrite(LCD_BL, 200);
}

#if ASTROLABE_WAVESHARE_S3_185_V2
struct St77916InitCommand {
  uint8_t cmd;
  uint8_t data[14];
  uint8_t len;
  uint16_t delayMs;
};

static const St77916InitCommand kWaveshare185cV2Init[] = {
  {0xF0, {0x28}, 1, 0}, {0xF2, {0x28}, 1, 0}, {0x73, {0xF0}, 1, 0}, {0x7C, {0xD1}, 1, 0},
  {0x83, {0xE0}, 1, 0}, {0x84, {0x61}, 1, 0}, {0xF2, {0x82}, 1, 0}, {0xF0, {0x00}, 1, 0},
  {0xF0, {0x01}, 1, 0}, {0xF1, {0x01}, 1, 0}, {0xB0, {0x56}, 1, 0}, {0xB1, {0x4D}, 1, 0},
  {0xB2, {0x24}, 1, 0}, {0xB4, {0x87}, 1, 0}, {0xB5, {0x44}, 1, 0}, {0xB6, {0x8B}, 1, 0},
  {0xB7, {0x40}, 1, 0}, {0xB8, {0x86}, 1, 0}, {0xBA, {0x00}, 1, 0}, {0xBB, {0x08}, 1, 0},
  {0xBC, {0x08}, 1, 0}, {0xBD, {0x00}, 1, 0}, {0xC0, {0x80}, 1, 0}, {0xC1, {0x10}, 1, 0},
  {0xC2, {0x37}, 1, 0}, {0xC3, {0x80}, 1, 0}, {0xC4, {0x10}, 1, 0}, {0xC5, {0x37}, 1, 0},
  {0xC6, {0xA9}, 1, 0}, {0xC7, {0x41}, 1, 0}, {0xC8, {0x01}, 1, 0}, {0xC9, {0xA9}, 1, 0},
  {0xCA, {0x41}, 1, 0}, {0xCB, {0x01}, 1, 0}, {0xD0, {0x91}, 1, 0}, {0xD1, {0x68}, 1, 0},
  {0xD2, {0x68}, 1, 0}, {0xF5, {0x00, 0xA5}, 2, 0}, {0xDD, {0x4F}, 1, 0}, {0xDE, {0x4F}, 1, 0},
  {0xF1, {0x10}, 1, 0}, {0xF0, {0x00}, 1, 0}, {0xF0, {0x02}, 1, 0},
  {0xE0, {0xF0, 0x0A, 0x10, 0x09, 0x09, 0x36, 0x35, 0x33, 0x4A, 0x29, 0x15, 0x15, 0x2E, 0x34}, 14, 0},
  {0xE1, {0xF0, 0x0A, 0x0F, 0x08, 0x08, 0x05, 0x34, 0x33, 0x4A, 0x39, 0x15, 0x15, 0x2D, 0x33}, 14, 0},
  {0xF0, {0x10}, 1, 0}, {0xF3, {0x10}, 1, 0}, {0xE0, {0x07}, 1, 0}, {0xE1, {0x00}, 1, 0},
  {0xE2, {0x00}, 1, 0}, {0xE3, {0x00}, 1, 0}, {0xE4, {0xE0}, 1, 0}, {0xE5, {0x06}, 1, 0},
  {0xE6, {0x21}, 1, 0}, {0xE7, {0x01}, 1, 0}, {0xE8, {0x05}, 1, 0}, {0xE9, {0x02}, 1, 0},
  {0xEA, {0xDA}, 1, 0}, {0xEB, {0x00}, 1, 0}, {0xEC, {0x00}, 1, 0}, {0xED, {0x0F}, 1, 0},
  {0xEE, {0x00}, 1, 0}, {0xEF, {0x00}, 1, 0}, {0xF8, {0x00}, 1, 0}, {0xF9, {0x00}, 1, 0},
  {0xFA, {0x00}, 1, 0}, {0xFB, {0x00}, 1, 0}, {0xFC, {0x00}, 1, 0}, {0xFD, {0x00}, 1, 0},
  {0xFE, {0x00}, 1, 0}, {0xFF, {0x00}, 1, 0}, {0x60, {0x40}, 1, 0}, {0x61, {0x04}, 1, 0},
  {0x62, {0x00}, 1, 0}, {0x63, {0x42}, 1, 0}, {0x64, {0xD9}, 1, 0}, {0x65, {0x00}, 1, 0},
  {0x66, {0x00}, 1, 0}, {0x67, {0x00}, 1, 0}, {0x68, {0x00}, 1, 0}, {0x69, {0x00}, 1, 0},
  {0x6A, {0x00}, 1, 0}, {0x6B, {0x00}, 1, 0}, {0x70, {0x40}, 1, 0}, {0x71, {0x03}, 1, 0},
  {0x72, {0x00}, 1, 0}, {0x73, {0x42}, 1, 0}, {0x74, {0xD8}, 1, 0}, {0x75, {0x00}, 1, 0},
  {0x76, {0x00}, 1, 0}, {0x77, {0x00}, 1, 0}, {0x78, {0x00}, 1, 0}, {0x79, {0x00}, 1, 0},
  {0x7A, {0x00}, 1, 0}, {0x7B, {0x00}, 1, 0}, {0x80, {0x48}, 1, 0}, {0x81, {0x00}, 1, 0},
  {0x82, {0x06}, 1, 0}, {0x83, {0x02}, 1, 0}, {0x84, {0xD6}, 1, 0}, {0x85, {0x04}, 1, 0},
  {0x86, {0x00}, 1, 0}, {0x87, {0x00}, 1, 0}, {0x88, {0x48}, 1, 0}, {0x89, {0x00}, 1, 0},
  {0x8A, {0x08}, 1, 0}, {0x8B, {0x02}, 1, 0}, {0x8C, {0xD8}, 1, 0}, {0x8D, {0x04}, 1, 0},
  {0x8E, {0x00}, 1, 0}, {0x8F, {0x00}, 1, 0}, {0x90, {0x48}, 1, 0}, {0x91, {0x00}, 1, 0},
  {0x92, {0x0A}, 1, 0}, {0x93, {0x02}, 1, 0}, {0x94, {0xDA}, 1, 0}, {0x95, {0x04}, 1, 0},
  {0x96, {0x00}, 1, 0}, {0x97, {0x00}, 1, 0}, {0x98, {0x48}, 1, 0}, {0x99, {0x00}, 1, 0},
  {0x9A, {0x0C}, 1, 0}, {0x9B, {0x02}, 1, 0}, {0x9C, {0xDC}, 1, 0}, {0x9D, {0x04}, 1, 0},
  {0x9E, {0x00}, 1, 0}, {0x9F, {0x00}, 1, 0}, {0xA0, {0x48}, 1, 0}, {0xA1, {0x00}, 1, 0},
  {0xA2, {0x05}, 1, 0}, {0xA3, {0x02}, 1, 0}, {0xA4, {0xD5}, 1, 0}, {0xA5, {0x04}, 1, 0},
  {0xA6, {0x00}, 1, 0}, {0xA7, {0x00}, 1, 0}, {0xA8, {0x48}, 1, 0}, {0xA9, {0x00}, 1, 0},
  {0xAA, {0x07}, 1, 0}, {0xAB, {0x02}, 1, 0}, {0xAC, {0xD7}, 1, 0}, {0xAD, {0x04}, 1, 0},
  {0xAE, {0x00}, 1, 0}, {0xAF, {0x00}, 1, 0}, {0xB0, {0x48}, 1, 0}, {0xB1, {0x00}, 1, 0},
  {0xB2, {0x09}, 1, 0}, {0xB3, {0x02}, 1, 0}, {0xB4, {0xD9}, 1, 0}, {0xB5, {0x04}, 1, 0},
  {0xB6, {0x00}, 1, 0}, {0xB7, {0x00}, 1, 0}, {0xB8, {0x48}, 1, 0}, {0xB9, {0x00}, 1, 0},
  {0xBA, {0x0B}, 1, 0}, {0xBB, {0x02}, 1, 0}, {0xBC, {0xDB}, 1, 0}, {0xBD, {0x04}, 1, 0},
  {0xBE, {0x00}, 1, 0}, {0xBF, {0x00}, 1, 0}, {0xC0, {0x10}, 1, 0}, {0xC1, {0x47}, 1, 0},
  {0xC2, {0x56}, 1, 0}, {0xC3, {0x65}, 1, 0}, {0xC4, {0x74}, 1, 0}, {0xC5, {0x88}, 1, 0},
  {0xC6, {0x99}, 1, 0}, {0xC7, {0x01}, 1, 0}, {0xC8, {0xBB}, 1, 0}, {0xC9, {0xAA}, 1, 0},
  {0xD0, {0x10}, 1, 0}, {0xD1, {0x47}, 1, 0}, {0xD2, {0x56}, 1, 0}, {0xD3, {0x65}, 1, 0},
  {0xD4, {0x74}, 1, 0}, {0xD5, {0x88}, 1, 0}, {0xD6, {0x99}, 1, 0}, {0xD7, {0x01}, 1, 0},
  {0xD8, {0xBB}, 1, 0}, {0xD9, {0xAA}, 1, 0}, {0xF3, {0x01}, 1, 0}, {0xF0, {0x00}, 1, 0},
  {0x21, {0x00}, 1, 0}, {0x11, {0x00}, 1, 120}, {0x29, {0x00}, 1, 0},
};

static void applyWaveshare185cV2PanelInit() {
  if (!s_displayBus) {
    return;
  }
  auto *qspi = static_cast<Arduino_ESP32QSPI *>(s_displayBus);
  ESP_LOGI(TAG, "applying Waveshare 1.85C V2 ST77916 init table");
  qspi->beginWrite();
  for (const auto &entry : kWaveshare185cV2Init) {
    qspi->writeC8Bytes(entry.cmd, const_cast<uint8_t *>(entry.data), entry.len);
    if (entry.delayMs) {
      qspi->endWrite();
      delay(entry.delayMs);
      qspi->beginWrite();
    }
  }
  qspi->endWrite();
}
#endif

struct AlbumArtRequest {
  char url[192];
};

static void drawDisplayStatus(const char *state, const char *detail = nullptr);

static void albumArtTask(void *arg) {
  AlbumArtRequest *req = static_cast<AlbumArtRequest *>(arg);
  if (req && req->url[0]) {
    ESP_LOGI(TAG, "album art fetching %s", req->url);
    uint8_t *bytes = nullptr;
    size_t len = 0;
    if (downloadHttps(req->url, &bytes, &len)) {
      if (!decodeAlbumJpeg(bytes, len)) {
        ESP_LOGW(TAG, "album art decode failed");
      } else if (s_displayFace == DisplayFace::Vinyl) {
        s_vinylFaceDrawn = false;
        s_vinylRenderedKey[0] = '\0';
        markVinylDirty("album art decoded");
        drawDisplayStatus(nullptr, nullptr);
      }
      free(bytes);
    } else {
      ESP_LOGW(TAG, "album art download failed");
    }
  }
  free(req);
  s_albumArtFetching = false;
  vTaskDelete(nullptr);
}

static void scheduleAlbumArt(const char *url) {
  if (!url || !url[0]) {
    ESP_LOGW(TAG, "album art missing URL");
    return;
  }
  if (s_albumArtFetching) {
    ESP_LOGI(TAG, "album art fetch already active");
    return;
  }
  if (strcmp(url, s_albumArtUrl) == 0 && s_albumArtReady) {
    ESP_LOGI(TAG, "album art already ready");
    return;
  }
  ESP_LOGI(TAG, "album art scheduled %s", url);
  copyTrunc(s_albumArtUrl, sizeof(s_albumArtUrl), url);
  if (takeMutex(&s_albumArtMutex, 100)) {
    if (s_albumArtFb) {
      free(s_albumArtFb);
      s_albumArtFb = nullptr;
    }
    s_albumArtReady = false;
    s_albumArtDrawLogged = false;
    giveMutex(s_albumArtMutex);
  }
  AlbumArtRequest *req = static_cast<AlbumArtRequest *>(calloc(1, sizeof(AlbumArtRequest)));
  if (!req) {
    return;
  }
  copyTrunc(req->url, sizeof(req->url), url);
  s_albumArtFetching = true;
  TaskHandle_t task = nullptr;
  if (xTaskCreatePinnedToCoreWithCaps(albumArtTask, "spotify_art", 48 * 1024, req, 1, &task, 1,
                                      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
    ESP_LOGW(TAG, "album art task create failed");
    s_albumArtFetching = false;
    free(req);
  }
}

static void drawAlbumArtOrFallback(int cx, int cy, int size, const PlayerFaceState &face) {
  const int x0 = cx - size / 2;
  const int y0 = cy - size / 2;
  s_display->fillRoundRect(x0 - 3, y0 - 3, size + 6, size + 6, 4, rgb565(20, 20, 24));
  bool drew = false;
  if (takeMutex(&s_albumArtMutex, 5)) {
    if (s_albumArtReady && s_albumArtFb) {
      for (int y = 0; y < size; ++y) {
        const int sy = y * kAlbumArtSize / size;
        for (int x = 0; x < size; ++x) {
          const int sx = x * kAlbumArtSize / size;
          s_display->drawPixel(x0 + x, y0 + y, s_albumArtFb[sy * kAlbumArtSize + sx]);
        }
      }
      drew = true;
    }
    giveMutex(s_albumArtMutex);
  }
  if (!drew) {
    s_display->fillRoundRect(x0, y0, size, size, 4, face.primary);
    for (int i = 0; i < 5; ++i) {
      const int yy = y0 + 10 + i * ((size - 18) / 4);
      s_display->drawFastHLine(x0 + 8, yy, size - 16, (i == 1) ? rgb565(248, 244, 220) : face.accent);
    }
  }
}

static void drawGrooves(int cx, int cy, int r, uint16_t color) {
  for (int i = 0; i < 17; ++i) {
    const int gr = r - 7 - i * 7;
    if (gr > 58) {
      s_display->drawCircle(cx, cy, gr, color);
    }
  }
}

static void drawAudioSpectrumRing(int cx, int cy, int r, const float *bands, int count, float level, float spin,
                                  uint16_t accent) {
  if (!bands || count <= 0) {
    return;
  }
  const int base = r + 3;
  for (int i = 0; i < count; ++i) {
    const float band = std::max(0.f, std::min(1.f, bands[i]));
    const float a = spin + (static_cast<float>(i) / static_cast<float>(count)) * 6.2831853f;
    const int inner = base;
    const int outer = r + 5 + static_cast<int>(band * (10.f + level * 14.f));
    const int x0 = cx + static_cast<int>(lrintf(cosf(a) * inner));
    const int y0 = cy + static_cast<int>(lrintf(sinf(a) * inner));
    const int x1 = cx + static_cast<int>(lrintf(cosf(a) * outer));
    const int y1 = cy + static_cast<int>(lrintf(sinf(a) * outer));
    const uint8_t glow = static_cast<uint8_t>(70 + band * 150.f);
    const uint16_t color = band > 0.52f ? accent : rgb565(glow / 2, glow / 2, glow);
    s_display->drawLine(x0, y0, x1, y1, color);
  }
}

static void emitStaffNoteFromBands(const float *bands, int count, float level, uint32_t now) {
  if (!bands || count <= 0 || level < 0.045f) {
    return;
  }

  float best = 0.f;
  int bestBand = -1;
  for (int i = 0; i < count; ++i) {
    const float midBias = 1.f - fabsf((static_cast<float>(i) / static_cast<float>(count - 1)) - 0.52f) * 0.34f;
    const float score = bands[i] * std::max(0.72f, midBias);
    if (score > best) {
      best = score;
      bestBand = i;
    }
  }

  const uint32_t minIntervalMs = 115u + static_cast<uint32_t>((1.f - std::min(1.f, level)) * 135.f);
  if (bestBand < 0 || best < 0.28f || now - s_lastStaffNoteMs < minIntervalMs) {
    return;
  }

  if (bestBand == s_lastStaffBand && best < 0.58f && now - s_lastStaffNoteMs < minIntervalMs + 130u) {
    return;
  }

  constexpr int staffTop = 46;
  constexpr int staffGap = 10;
  const int pitch = std::max(0, std::min(14, (bestBand * 15) / count));
  StaffNote &note = s_staffNotes[s_staffNoteHead++ % (sizeof(s_staffNotes) / sizeof(s_staffNotes[0]))];
  note.bornMs = now;
  note.y = staffTop + staffGap * 2 + (7 - pitch) * (staffGap / 2);
  note.y = std::max(22, std::min(LCD_HEIGHT - 24, note.y));
  note.velocity = std::max(0.2f, std::min(1.f, best * 0.85f + level * 0.35f));
  const uint8_t warm = static_cast<uint8_t>(150 + note.velocity * 90.f);
  const uint8_t cool = static_cast<uint8_t>(120 + (static_cast<float>(bestBand) / count) * 110.f);
  note.color = bestBand > count / 2 ? rgb565(warm, 210, cool) : rgb565(120, cool, warm);
  note.sharp = (bestBand % 5) == 2 || (bestBand % 7) == 4;
  s_lastStaffNoteMs = now;
  s_lastStaffBand = bestBand;
}

static void drawStaffNotehead(int x, int y, uint16_t color, float velocity, bool sharp) {
  const int radius = 3 + static_cast<int>(velocity * 3.f);
  s_display->fillCircle(x, y, radius, color);
  s_display->drawCircle(x, y, radius + 1, rgb565(246, 242, 218));

  const bool stemUp = y >= LCD_HEIGHT / 2;
  const int stemX = stemUp ? x + radius + 2 : x - radius - 2;
  const int stemY = stemUp ? y - 30 : y + 30;
  s_display->drawLine(stemX, y, stemX, stemY, rgb565(225, 226, 214));
  s_display->drawLine(stemX + (stemUp ? 1 : -1), y, stemX + (stemUp ? 1 : -1), stemY, rgb565(120, 200, 210));
  if (velocity > 0.55f) {
    const int flagDir = stemUp ? 1 : -1;
    s_display->drawLine(stemX, stemY, stemX + 12 * flagDir, stemY + (stemUp ? 7 : -7), color);
    s_display->drawLine(stemX, stemY + (stemUp ? 4 : -4), stemX + 10 * flagDir, stemY + (stemUp ? 10 : -10),
                        rgb565(170, 230, 235));
  }

  if (sharp) {
    const int sx = x - radius - 10;
    s_display->drawFastVLine(sx + 2, y - 8, 16, rgb565(180, 185, 182));
    s_display->drawFastVLine(sx + 7, y - 9, 16, rgb565(180, 185, 182));
    s_display->drawFastHLine(sx, y - 3, 11, rgb565(180, 185, 182));
    s_display->drawFastHLine(sx, y + 3, 11, rgb565(180, 185, 182));
  }
}

static void drawStaffVisualizer(const float *bands, int count, float level, uint32_t now) {
  constexpr int staffTop = 46;
  constexpr int staffGap = 10;
  constexpr int staffLeft = 18;
  constexpr int staffRight = LCD_WIDTH - 10;
  constexpr int staffBottom = staffTop + staffGap * 4;

  emitStaffNoteFromBands(bands, count, level, now);
  s_display->fillRect(0, staffTop - 18, LCD_WIDTH, staffGap * 7 + 18, 0x0000);

  for (int y = staffTop; y <= staffBottom; y += staffGap) {
    const uint16_t lineColor = y == staffTop + staffGap * 2 ? rgb565(64, 75, 78) : rgb565(42, 48, 50);
    s_display->drawFastHLine(staffLeft, y, staffRight - staffLeft, lineColor);
  }
  for (int x = staffLeft + 28; x < staffRight; x += 40) {
    s_display->drawFastVLine(x, staffTop - 3, staffGap * 4 + 7, rgb565(18, 24, 26));
  }

  s_display->drawCircle(10, staffTop + staffGap * 2, 8, rgb565(86, 210, 205));
  s_display->drawCircle(12, staffTop + staffGap * 2, 13, rgb565(46, 92, 96));
  s_display->drawFastVLine(18, staffTop - 7, staffGap * 5 + 2, rgb565(100, 210, 208));

  for (const StaffNote &note : s_staffNotes) {
    if (note.bornMs == 0) {
      continue;
    }
    const uint32_t age = now - note.bornMs;
    if (age > 3300u) {
      continue;
    }
    const int x = LCD_WIDTH - static_cast<int>((static_cast<uint64_t>(age) * (LCD_WIDTH + 38)) / 3300u);
    if (x < -18 || x > LCD_WIDTH + 14) {
      continue;
    }
    if (note.y < staffTop - staffGap / 2) {
      for (int y = staffTop - staffGap; y >= note.y - 2; y -= staffGap) {
        s_display->drawFastHLine(x - 9, y, 18, rgb565(55, 62, 64));
      }
    } else if (note.y > staffBottom + staffGap / 2) {
      for (int y = staffBottom + staffGap; y <= note.y + 2; y += staffGap) {
        s_display->drawFastHLine(x - 9, y, 18, rgb565(55, 62, 64));
      }
    }
    drawStaffNotehead(x, note.y, note.color, note.velocity, note.sharp);
  }

  const int pulse = static_cast<int>(level * 34.f);
  s_display->drawCircle(LCD_WIDTH - 28, LCD_HEIGHT - 25, 8 + pulse / 4, rgb565(60 + pulse, 150, 160));
  s_display->fillCircle(LCD_WIDTH - 28, LCD_HEIGHT - 25, 3 + pulse / 8, rgb565(220, 236, 218));
}

static float recordSpinRadians(const PlayerFaceState &face, float level) {
  const uint32_t now = millis();
  const uint32_t spinPeriodMs = face.playing ? 1333u : 12000u;
  const uint32_t spinSource = face.playing ? now : face.progressMs;
  return (static_cast<float>(spinSource % spinPeriodMs) / static_cast<float>(spinPeriodMs)) * 6.2831853f;
}

static void flushVinylLabelRadialLocked(float spin) {
  constexpr int cx = LCD_WIDTH / 2;
  constexpr int cy = LCD_HEIGHT / 2;
  constexpr int labelR = kVinylLabelRadius;
  constexpr int frame = kVinylLabelFrame;
  constexpr int maxR = labelR + 2;
  constexpr int kAngularSteps = 360;
  constexpr float kTwoPi = 6.2831853f;
  int start = static_cast<int>(spin * (static_cast<float>(kAngularSteps) / kTwoPi));
  start = (start % kAngularSteps + kAngularSteps) % kAngularSteps;

  s_display->startWrite();
  for (int i = 0; i < kAngularSteps; ++i) {
    const int ai = (start + i) % kAngularSteps;
    const float a = (static_cast<float>(ai) / static_cast<float>(kAngularSteps)) * kTwoPi;
    const float ca = cosf(a);
    const float sa = sinf(a);
    int lastX = 9999;
    int lastY = 9999;
    for (int rr = 0; rr <= maxR; ++rr) {
      const int dx = static_cast<int>(lrintf(ca * rr));
      const int dy = static_cast<int>(lrintf(sa * rr));
      if (dx == lastX && dy == lastY) {
        continue;
      }
      lastX = dx;
      lastY = dy;
      const int ix = dx + labelR + 2;
      const int iy = dy + labelR + 2;
      if (ix >= 0 && ix < frame && iy >= 0 && iy < frame) {
        s_display->writePixel(cx + dx, cy + dy, s_vinylLabelFb[iy * frame + ix]);
      }
    }
  }
  s_display->endWrite();
}

static void flushVinylLabelBitmapLocked() {
  constexpr int cx = LCD_WIDTH / 2;
  constexpr int cy = LCD_HEIGHT / 2;
  constexpr int labelR = kVinylLabelRadius;
  constexpr int frame = kVinylLabelFrame;
  constexpr int maxR = labelR + 2;

  s_display->startWrite();
  for (int yy = -maxR; yy <= maxR; ++yy) {
    const int dy2 = yy * yy;
    for (int xx = -maxR; xx <= maxR; ++xx) {
      if (xx * xx + dy2 > maxR * maxR) {
        continue;
      }
      const int ix = xx + labelR + 2;
      const int iy = yy + labelR + 2;
      if (ix >= 0 && ix < frame && iy >= 0 && iy < frame) {
        s_display->writePixel(cx + xx, cy + yy, s_vinylLabelFb[iy * frame + ix]);
      }
    }
  }
  s_display->endWrite();
}

static void drawRotatingAlbumDiskLocked(const PlayerFaceState &face, float spin) {
  constexpr int cx = LCD_WIDTH / 2;
  constexpr int cy = LCD_HEIGHT / 2;
  constexpr int labelR = kVinylLabelRadius;
  constexpr int frame = kVinylLabelFrame;
  const bool haveArt = takeMutex(&s_albumArtMutex, 5);
  const bool drawArt = haveArt && s_albumArtReady && s_albumArtFb;
  const float c = 1.f;
  const float s = 0.f;
  const uint16_t labelBg = rgb565(232, 230, 218);
  const uint16_t labelEdge = rgb565(190, 188, 178);
  const uint16_t holeBg = rgb565(242, 241, 232);
  const uint16_t holeEdge = rgb565(170, 168, 160);
  for (int yy = -labelR - 2; yy <= labelR + 2; ++yy) {
    const int dy2 = yy * yy;
    for (int xx = -labelR - 2; xx <= labelR + 2; ++xx) {
      const int rr = xx * xx + dy2;
      uint16_t color = labelBg;
      if (rr > (labelR + 2) * (labelR + 2)) {
        s_vinylLabelFb[(yy + labelR + 2) * frame + (xx + labelR + 2)] = color;
        continue;
      } else if (rr > labelR * labelR) {
        color = labelEdge;
      } else if (rr <= kVinylHoleRadius * kVinylHoleRadius) {
        color = holeBg;
      } else if (rr <= (kVinylHoleRadius + 2) * (kVinylHoleRadius + 2)) {
        color = holeEdge;
      } else if (drawArt) {
        if (!s_albumArtDrawLogged && xx == -labelR && yy == 0) {
          ESP_LOGI(TAG, "vinyl drawing album art %s", s_albumArtUrl);
          s_albumArtDrawLogged = true;
        }
        const float rx = static_cast<float>(xx) * c - static_cast<float>(yy) * s;
        const float ry = static_cast<float>(xx) * s + static_cast<float>(yy) * c;
        const int sx = static_cast<int>(lrintf((rx / static_cast<float>(labelR) + 1.f) * 0.5f * (kAlbumArtSize - 1)));
        const int sy = static_cast<int>(lrintf((ry / static_cast<float>(labelR) + 1.f) * 0.5f * (kAlbumArtSize - 1)));
        if (sx >= 0 && sx < kAlbumArtSize && sy >= 0 && sy < kAlbumArtSize) {
          color = s_albumArtFb[sy * kAlbumArtSize + sx];
        } else {
          color = face.primary;
        }
      } else {
        const float rx = static_cast<float>(xx) * c - static_cast<float>(yy) * s;
        const float ry = static_cast<float>(xx) * s + static_cast<float>(yy) * c;
        const float theta = atan2f(ry, rx);
        const float radius = sqrtf(static_cast<float>(rr)) / static_cast<float>(labelR);
        const float stripe = sinf(theta * 2.0f + radius * 8.0f);
        const uint8_t shade = static_cast<uint8_t>(205 + std::max(0.f, stripe) * 22.f);
        color = rgb565(shade, shade, static_cast<uint8_t>(std::max(190, shade - 8)));
      }
      s_vinylLabelFb[(yy + labelR + 2) * frame + (xx + labelR + 2)] = color;
    }
  }
  if (haveArt) {
    giveMutex(s_albumArtMutex);
  }
  flushVinylLabelBitmapLocked();
  s_display->drawCircle(cx, cy, kVinylHoleRadius + 3, holeEdge);
  s_display->fillCircle(cx, cy, kVinylHoleRadius, holeBg);
}

static void clippedArcText(char *out, size_t cap, const char *text, int maxChars) {
  if (!out || cap == 0) {
    return;
  }
  const char *source = text && text[0] ? text : "Spotify";
  while (*source == ' ') {
    ++source;
  }
  clippedText(out, cap, source, std::max(3, maxChars));
  for (char *p = out; *p; ++p) {
    *p = static_cast<char>(toupper(static_cast<unsigned char>(*p)));
  }
}

static void drawRotatedGlyphPixel(int cx, int cy, float angle, float radial, float tangent, uint16_t color, int scale) {
  const float ca = cosf(angle);
  const float sa = sinf(angle);
  const int x = cx + static_cast<int>(lrintf(ca * radial - sa * tangent));
  const int y = cy + static_cast<int>(lrintf(sa * radial + ca * tangent));
  if (scale <= 1) {
    s_display->writePixel(x, y, color);
  } else {
    s_display->writeFillRect(x, y, scale, scale, color);
  }
}

static void drawArcGlyph(char c, int cx, int cy, float angle, int radius, int scale, uint16_t color) {
  if (c < 32 || c > 126) {
    c = '?';
  }
  constexpr int kGlyphW = 5;
  constexpr int kGlyphH = 7;
  const float halfW = static_cast<float>(kGlyphW * scale) * 0.5f;
  const float halfH = static_cast<float>(kGlyphH * scale) * 0.5f;
  for (int gx = 0; gx < kGlyphW; ++gx) {
    const uint8_t line = pgm_read_byte(&font[static_cast<uint8_t>(c) * 5 + gx]);
    for (int gy = 0; gy < kGlyphH; ++gy) {
      if (!(line & (1 << gy))) {
        continue;
      }
      for (int sx = 0; sx < scale; ++sx) {
        for (int sy = 0; sy < scale; ++sy) {
          const float tangent = static_cast<float>(gx * scale + sx) - halfW;
          const float radial = static_cast<float>(radius) + halfH - static_cast<float>(gy * scale + sy);
          drawRotatedGlyphPixel(cx, cy, angle, radial, tangent, color, 1);
        }
      }
    }
  }
}

static void drawArcLabelText(const char *text, int radius, float centerAngle, float maxArcRadians, int scale, uint16_t color,
                             uint16_t shadow) {
  char clipped[36];
  const int charAdvance = 6 * scale;
  const int maxChars = std::max(4, static_cast<int>((maxArcRadians * radius) / charAdvance));
  clippedArcText(clipped, sizeof(clipped), text, std::min(maxChars, static_cast<int>(sizeof(clipped) - 1)));
  const int n = static_cast<int>(strlen(clipped));
  if (n <= 0) {
    return;
  }
  const float step = static_cast<float>(charAdvance) / static_cast<float>(radius);
  const float start = centerAngle - (static_cast<float>(n - 1) * step * 0.5f);
  s_display->startWrite();
  for (int i = 0; i < n; ++i) {
    const float a = start + static_cast<float>(i) * step;
    drawArcGlyph(clipped[i], LCD_WIDTH / 2, LCD_HEIGHT / 2, a, radius + 1, scale, shadow);
  }
  for (int i = 0; i < n; ++i) {
    const float a = start + static_cast<float>(i) * step;
    drawArcGlyph(clipped[i], LCD_WIDTH / 2, LCD_HEIGHT / 2, a, radius, scale, color);
  }
  s_display->endWrite();
}

static uint16_t albumArtSampleLocked(int x, int y, const PlayerFaceState &face) {
  if (s_albumArtReady && s_albumArtFb) {
    const int artLeft = LCD_WIDTH / 2 - kVinylBezelInnerRadius;
    const int artTop = LCD_HEIGHT / 2 - kVinylBezelInnerRadius;
    const int artSize = kVinylBezelInnerRadius * 2 + 1;
    if (x < artLeft || x >= artLeft + artSize || y < artTop || y >= artTop + artSize) {
      return 0x0000;
    }
    const int sx = std::max(0, std::min(kAlbumArtSize - 1, (x - artLeft) * kAlbumArtSize / artSize));
    const int sy = std::max(0, std::min(kAlbumArtSize - 1, (y - artTop) * kAlbumArtSize / artSize));
    return s_albumArtFb[sy * kAlbumArtSize + sx];
  }
  (void)x;
  (void)y;
  (void)face;
  return 0x0000;
}

static uint16_t dimColor565(uint16_t color, uint8_t numerator, uint8_t denominator) {
  const uint8_t r = static_cast<uint8_t>(((color >> 11) & 0x1f) * 255 / 31);
  const uint8_t g = static_cast<uint8_t>(((color >> 5) & 0x3f) * 255 / 63);
  const uint8_t b = static_cast<uint8_t>((color & 0x1f) * 255 / 31);
  return rgb565(static_cast<uint8_t>((r * numerator) / denominator),
                static_cast<uint8_t>((g * numerator) / denominator),
                static_cast<uint8_t>((b * numerator) / denominator));
}

static void drawFullScreenAlbumArtLocked(const PlayerFaceState &face) {
  constexpr int cx = LCD_WIDTH / 2;
  constexpr int cy = LCD_HEIGHT / 2;
  constexpr int maxR = kVinylBezelInnerRadius;
  const bool haveArt = takeMutex(&s_albumArtMutex, 20);
  ESP_LOGI(TAG, "vinyl full art draw ready=%d url='%s'", s_albumArtReady ? 1 : 0, s_albumArtUrl);
  s_display->startWrite();
  for (int r = 0; r <= maxR; ++r) {
    const int outer2 = r * r;
    const int inner = std::max(0, r - 1);
    const int inner2 = inner * inner;
    for (int dy = -r; dy <= r; ++dy) {
      const int y = cy + dy;
      if (y < 0 || y >= LCD_HEIGHT) {
        continue;
      }
      const int dy2 = dy * dy;
      if (dy2 > outer2) {
        continue;
      }
      const int outerX = static_cast<int>(sqrtf(static_cast<float>(outer2 - dy2)));
      const int innerX = dy2 < inner2 ? static_cast<int>(sqrtf(static_cast<float>(inner2 - dy2))) : -1;
      for (int side = -1; side <= 1; side += 2) {
        const int start = side < 0 ? -outerX : innerX + 1;
        const int end = side < 0 ? -innerX - 1 : outerX;
        for (int dx = start; dx <= end; ++dx) {
          const int x = cx + dx;
          if (x >= 0 && x < LCD_WIDTH) {
            s_display->writePixel(x, y, albumArtSampleLocked(x, y, face));
          }
        }
      }
    }
    if ((r & 0x0f) == 0x0f) {
      s_display->endWrite();
      vTaskDelay(1);
      s_display->startWrite();
    }
  }
  s_display->endWrite();
  if (haveArt) {
    giveMutex(s_albumArtMutex);
  }
}

static void restoreAlbumArtRingRadialLocked(const PlayerFaceState &face, int innerRadius, int outerRadius, float phase) {
  constexpr int cx = LCD_WIDTH / 2;
  constexpr int cy = LCD_HEIGHT / 2;
  constexpr float kTwoPi = 6.2831853f;
  constexpr int kAngularSteps = 720;
  const int start = static_cast<int>(phase * (static_cast<float>(kAngularSteps) / kTwoPi));
  const bool haveArt = takeMutex(&s_albumArtMutex, 20);
  s_display->startWrite();
  for (int i = 0; i < kAngularSteps; ++i) {
    const int ai = (start + i) % kAngularSteps;
    const float a = (static_cast<float>(ai) / static_cast<float>(kAngularSteps)) * kTwoPi;
    const float ca = cosf(a);
    const float sa = sinf(a);
    int lastX = 10000;
    int lastY = 10000;
    for (int rr = innerRadius; rr <= outerRadius; ++rr) {
      const int x = cx + static_cast<int>(lrintf(ca * rr));
      const int y = cy + static_cast<int>(lrintf(sa * rr));
      if (x == lastX && y == lastY) {
        continue;
      }
      lastX = x;
      lastY = y;
      if (x < 0 || x >= LCD_WIDTH || y < 0 || y >= LCD_HEIGHT) {
        continue;
      }
      uint16_t color = albumArtSampleLocked(x, y, face);
      if (rr > outerRadius - 3) {
        color = rgb565(2, 2, 4);
      } else if (rr < innerRadius + 5) {
        color = dimColor565(color, 3, 5);
      }
      s_display->writePixel(x, y, color);
    }
    if ((i % 96) == 95) {
      s_display->endWrite();
      vTaskDelay(1);
      s_display->startWrite();
    }
  }
  s_display->endWrite();
  if (haveArt) {
    giveMutex(s_albumArtMutex);
  }
}

static void buildBezelText(const PlayerFaceState &face, char *out, size_t cap) {
  if (!out || cap == 0) {
    return;
  }
  char title[56];
  char artist[40];
  clippedArcText(title, sizeof(title), face.title, 42);
  clippedArcText(artist, sizeof(artist), face.artist[0] ? face.artist : face.album, 28);
  snprintf(out, cap, "%s / %s", title, artist[0] ? artist : "SPOTIFY");
}

static void drawBezelBandLocked(int innerRadius, int outerRadius) {
  constexpr int cx = LCD_WIDTH / 2;
  constexpr int cy = LCD_HEIGHT / 2;
  const int inner2 = innerRadius * innerRadius;
  const int outer2 = outerRadius * outerRadius;
  s_display->startWrite();
  for (int y = cy - outerRadius; y <= cy + outerRadius; ++y) {
    if (y < 0 || y >= LCD_HEIGHT) {
      continue;
    }
    const int dy = y - cy;
    const int dy2 = dy * dy;
    int lastStart = -1;
    for (int x = cx - outerRadius; x <= cx + outerRadius; ++x) {
      if (x < 0 || x >= LCD_WIDTH) {
        continue;
      }
      const int dx = x - cx;
      const int r2 = dx * dx + dy2;
      const bool inBand = r2 >= inner2 && r2 <= outer2;
      if (inBand && lastStart < 0) {
        lastStart = x;
      } else if (!inBand && lastStart >= 0) {
        s_display->writeFastHLine(lastStart, y, x - lastStart, 0x0000);
        lastStart = -1;
      }
    }
    if (lastStart >= 0) {
      s_display->writeFastHLine(lastStart, y, std::min(LCD_WIDTH, cx + outerRadius + 1) - lastStart, 0x0000);
    }
  }
  s_display->endWrite();
}

static void drawBezelTextLocked(const PlayerFaceState &face, float centerAngle) {
  constexpr float kTwoPi = 6.2831853f;
  constexpr int textScale = 2;
  constexpr int textRadius = (LCD_WIDTH < LCD_HEIGHT ? LCD_WIDTH : LCD_HEIGHT) / 2 - 22;
  char segment[112];
  buildBezelText(face, segment, sizeof(segment));
  const int segmentLen = static_cast<int>(strlen(segment));
  if (segmentLen == 0) {
    return;
  }
  const int charAdvance = 6 * textScale;
  const float step = static_cast<float>(charAdvance) / static_cast<float>(textRadius);
  const float start = centerAngle - (static_cast<float>(segmentLen - 1) * step * 0.5f);
  s_display->startWrite();
  for (int i = 0; i < segmentLen; ++i) {
    const char c = segment[i];
    float angle = start + static_cast<float>(i) * step;
    while (angle > 3.14159265f) {
      angle -= kTwoPi;
    }
    while (angle < -3.14159265f) {
      angle += kTwoPi;
    }
    drawArcGlyph(c, LCD_WIDTH / 2, LCD_HEIGHT / 2, angle, textRadius + 2, textScale, rgb565(0, 0, 0));
  }
  for (int i = 0; i < segmentLen; ++i) {
    const char c = segment[i];
    float angle = start + static_cast<float>(i) * step;
    while (angle > 3.14159265f) {
      angle -= kTwoPi;
    }
    while (angle < -3.14159265f) {
      angle += kTwoPi;
    }
    drawArcGlyph(c, LCD_WIDTH / 2, LCD_HEIGHT / 2, angle, textRadius, textScale, rgb565(255, 249, 214));
  }
  s_display->endWrite();
}

static void drawVinylBezelOverlayLocked(const PlayerFaceState &face) {
  constexpr float centerAngle = -1.5707963f;
  drawBezelBandLocked(kVinylBezelInnerRadius, kVinylBezelOuterRadius);
  drawBezelTextLocked(face, centerAngle);
}

static void drawVisualizerLocked(uint8_t mode) {
  constexpr int cx = LCD_WIDTH / 2;
  constexpr int cy = LCD_HEIGHT / 2;
  constexpr int r = (LCD_WIDTH < LCD_HEIGHT ? LCD_WIDTH : LCD_HEIGHT) / 2 - 16;
  float bands[ASTROLABE_AUDIO_VIS_BANDS] = {};
  float wave[ASTROLABE_AUDIO_VIS_WAVE_POINTS] = {};
  float level = 0.f;
  astrolabe_audio_visualizer_get(bands, ASTROLABE_AUDIO_VIS_BANDS, wave, ASTROLABE_AUDIO_VIS_WAVE_POINTS, &level);
  for (int i = 0; i < ASTROLABE_AUDIO_VIS_BANDS; ++i) {
    s_smoothBands[i] = s_smoothBands[i] * 0.82f + bands[i] * 0.18f;
    bands[i] = s_smoothBands[i];
  }
  for (int i = 0; i < ASTROLABE_AUDIO_VIS_WAVE_POINTS; ++i) {
    s_smoothWave[i] = s_smoothWave[i] * 0.78f + wave[i] * 0.22f;
    wave[i] = s_smoothWave[i];
  }
  s_smoothLevel = s_smoothLevel * 0.86f + level * 0.14f;
  level = s_smoothLevel;
  const uint32_t now = millis();
  const float spin = (static_cast<float>(now % 9000u) / 9000.f) * 6.2831853f;

  if (s_visualizerNeedsClear) {
    s_display->fillScreen(0x0000);
    s_visualizerNeedsClear = false;
  }
  s_display->drawCircle(cx, cy, r, rgb565(18, 22, 26));

  if (mode == 0) {
    s_display->fillCircle(cx, cy, 12 + static_cast<int>(level * 18.f), rgb565(2, 9, 10));
    drawAudioSpectrumRing(cx, cy, r - 18, bands, ASTROLABE_AUDIO_VIS_BANDS, level * 0.65f, spin, rgb565(62, 145, 150));
    drawAudioSpectrumRing(cx, cy, r - 48, bands, ASTROLABE_AUDIO_VIS_BANDS, level * 0.55f, -spin, rgb565(150, 134, 78));
  } else if (mode == 1) {
    s_display->fillRect(0, cy - 72, LCD_WIDTH, 144, 0x0000);
    int px = 0;
    int py = cy;
    for (int i = 0; i < ASTROLABE_AUDIO_VIS_WAVE_POINTS; ++i) {
      const int x = 10 + i * (LCD_WIDTH - 20) / (ASTROLABE_AUDIO_VIS_WAVE_POINTS - 1);
      const int y = cy + static_cast<int>(wave[i] * (32.f + level * 18.f));
      if (i > 0) {
        s_display->drawLine(px, py, x, y, rgb565(96, 170, 188));
      }
      px = x;
      py = y;
    }
  } else if (mode == 2) {
    s_display->fillRect(0, 28, LCD_WIDTH, LCD_HEIGHT - 44, 0x0000);
    for (int i = 0; i < ASTROLABE_AUDIO_VIS_BANDS; ++i) {
      const float v = std::max(0.f, std::min(1.f, bands[i]));
      const int h = static_cast<int>(v * (LCD_HEIGHT - 72));
      const int x = 10 + i * (LCD_WIDTH - 20) / ASTROLABE_AUDIO_VIS_BANDS;
      const int w = std::max(3, (LCD_WIDTH - 28) / ASTROLABE_AUDIO_VIS_BANDS);
      s_display->fillRoundRect(x, LCD_HEIGHT - 16 - h, w, h, 2,
                               v > 0.62f ? rgb565(160, 130, 70) : rgb565(54, 124, 142));
    }
  } else {
    drawStaffVisualizer(bands, ASTROLABE_AUDIO_VIS_BANDS, level, now);
  }
}

static const char *instrumentName() {
  switch (s_instrumentMode % 3) {
    case 0:
      return "Kalimba";
    case 1:
      return "Bells";
    default:
      return "Bowl";
  }
}

static void drawInstrumentFaceLocked() {
  constexpr int cx = LCD_WIDTH / 2;
  constexpr int cy = LCD_HEIGHT / 2;
  constexpr int r = (LCD_WIDTH < LCD_HEIGHT ? LCD_WIDTH : LCD_HEIGHT) / 2 - 12;
  const uint32_t now = millis();
  s_display->fillScreen(0x0000);
  s_display->drawCircle(cx, cy, r, rgb565(36, 58, 62));
  s_display->drawCircle(cx, cy, r - 22, rgb565(18, 30, 34));
  for (int i = 0; i < 8; ++i) {
    const float a = -1.5707963f + static_cast<float>(i) * 6.2831853f / 8.f;
    const int x = cx + static_cast<int>(cosf(a) * (r - 42));
    const int y = cy + static_cast<int>(sinf(a) * (r - 42));
    const bool hot = i == s_lastInstrumentNote && now - s_lastInstrumentNoteMs < 260u;
    const uint16_t col = hot ? rgb565(255, 232, 150) : rgb565(70, 132, 142);
    s_display->fillCircle(x, y, hot ? 12 : 7, col);
    s_display->drawCircle(x, y, hot ? 16 : 11, rgb565(14, 40, 44));
  }
  drawCentered(instrumentName(), cy - 16, rgb565(245, 240, 215), 2);
  drawCentered("play along", cy + 12, rgb565(116, 166, 170), 1);
}

static void drawRecordFaceLocked(const PlayerFaceState &face) {
  ESP_LOGI(TAG, "vinyl draw title='%s' artist='%s' artReady=%d", face.title, face.artist, s_albumArtReady ? 1 : 0);
  if (!s_albumArtReady) {
    s_display->fillScreen(0x0000);
    drawCentered(face.state[0] ? face.state : "Spotify", LCD_HEIGHT / 2 - 14, rgb565(245, 240, 215), 2);
    if (face.artist[0]) {
      drawCentered(face.artist, LCD_HEIGHT / 2 + 12, rgb565(130, 135, 138), 1);
    }
    s_vinylFaceDrawn = true;
    s_vinylHasArtworkFrame = false;
    s_lastVinylTextFrameMs = millis();
    return;
  }
  drawFullScreenAlbumArtLocked(face);
  drawVinylBezelOverlayLocked(face);
  s_vinylFaceDrawn = true;
  s_vinylHasArtworkFrame = true;
  s_lastVinylTextFrameMs = millis();
}

static void drawRecordAnimationOverlay(const PlayerFaceState &face) {
  if (!s_display || !takeMutex(&s_displayMutex, 20)) {
    return;
  }
  if (!s_vinylHasArtworkFrame || !s_albumArtReady) {
    giveMutex(s_displayMutex);
    return;
  }
  drawVinylBezelOverlayLocked(face);
  s_lastVinylTextFrameMs = millis();
  giveMutex(s_displayMutex);
}

static void buildVinylRenderKey(const PlayerFaceState &face, char *out, size_t outSize) {
  snprintf(out, outSize, "%s|%s|%s|%s|%d|%d|%s", face.state, face.title, face.artist, face.artUrl,
           s_albumArtReady ? 1 : 0, face.playing ? 1 : 0, s_albumArtReady ? s_albumArtUrl : "");
}

static void drawDisplayStatus(const char *state, const char *detail) {
  if (!s_display) {
    return;
  }
  if (state && state[0]) {
    if (takeMutex(&s_faceMutex, 50)) {
      const bool preserveCurrentArt =
          strcmp(state, "Ready in Spotify") == 0 && s_displayFace == DisplayFace::Vinyl &&
          (s_albumArtReady || s_face.artUrl[0]);
      if (preserveCurrentArt) {
        giveMutex(s_faceMutex);
        return;
      }
      copyTrunc(s_face.state, sizeof(s_face.state), state);
      if (strcmp(state, "Playing") == 0 || strcmp(state, "Seeking") == 0 || strcmp(state, "Next") == 0 ||
          strcmp(state, "Previous") == 0) {
        s_face.playing = true;
      } else if (strcmp(state, "Paused") == 0 || strcmp(state, "Ready in Spotify") == 0 ||
                 strcmp(state, "Spotify auth failed") == 0 || strcmp(state, "Pair in Spotify") == 0) {
        s_face.playing = false;
      }
      const bool statusShouldShowDeviceName =
          strcmp(state, "Pair in Spotify") == 0 || strcmp(state, "Ready in Spotify") == 0 ||
          strcmp(state, "Authenticating") == 0 || strcmp(state, "Spotify auth failed") == 0 ||
          strcmp(state, "Spotify reconnect") == 0 || strcmp(state, "WiFi connected") == 0;
      if (detail && detail[0] && (statusShouldShowDeviceName || strcmp(detail, astrolabeDeviceName()) != 0)) {
        copyTrunc(s_face.artist, sizeof(s_face.artist), detail);
      }
      if (s_displayFace == DisplayFace::Vinyl && s_vinylFaceDrawn && !s_vinylHasArtworkFrame) {
        markVinylDirty("status changed before artwork");
      }
      giveMutex(s_faceMutex);
    }
  }
  if (s_displayFace == DisplayFace::Vinyl && state && s_vinylFaceDrawn && s_vinylHasArtworkFrame) {
    return;
  }
  if (!takeMutex(&s_displayMutex, 200)) {
    return;
  }
  PlayerFaceState face;
  if (takeMutex(&s_faceMutex, 50)) {
    face = s_face;
    giveMutex(s_faceMutex);
  } else {
    copyTrunc(face.state, sizeof(face.state), state ? state : "Starting");
    copyTrunc(face.title, sizeof(face.title), "Astrolabe");
    copyTrunc(face.artist, sizeof(face.artist), detail ? detail : "Spotify Connect");
    face.primary = hashColor(face.title, 0);
    face.accent = hashColor(face.title, 1);
  }
  if (s_displayFace == DisplayFace::Visualizer) {
    drawVisualizerLocked(s_visualizerMode);
  } else if (s_displayFace == DisplayFace::Instrument) {
    drawInstrumentFaceLocked();
  } else {
    if (s_vinylFaceDrawn && !s_vinylDirty) {
      giveMutex(s_displayMutex);
      return;
    }
    char key[sizeof(s_vinylRenderedKey)];
    buildVinylRenderKey(face, key, sizeof(key));
    if (s_vinylFaceDrawn && strcmp(key, s_vinylRenderedKey) == 0) {
      s_vinylDirty = false;
      giveMutex(s_displayMutex);
      return;
    }
    drawRecordFaceLocked(face);
    copyTrunc(s_vinylRenderedKey, sizeof(s_vinylRenderedKey), key);
    s_vinylDirty = false;
  }
  giveMutex(s_displayMutex);
}

static void changeDisplayFace(int delta) {
  if (delta == 0) {
    return;
  }
  int next = static_cast<int>(s_displayFace) + (delta > 0 ? 1 : -1);
  next = (next % 3 + 3) % 3;
  s_displayFace = static_cast<DisplayFace>(next);
  s_visualizerNeedsClear = true;
  if (s_displayFace == DisplayFace::Vinyl) {
    s_vinylRenderedKey[0] = '\0';
    markVinylDirty("face selected");
  }
  drawDisplayStatus(nullptr, nullptr);
}

static void changeVisualizerMode(int delta) {
  if (delta == 0 || s_displayFace != DisplayFace::Visualizer) {
    return;
  }
  constexpr int kModes = 4;
  int next = static_cast<int>(s_visualizerMode) + delta;
  next = (next % kModes + kModes) % kModes;
  s_visualizerMode = static_cast<uint8_t>(next);
  s_visualizerNeedsClear = true;
  drawDisplayStatus(nullptr, nullptr);
}

static void changeInstrumentMode(int delta) {
  if (delta == 0 || s_displayFace != DisplayFace::Instrument) {
    return;
  }
  constexpr int kModes = 3;
  int next = static_cast<int>(s_instrumentMode) + delta;
  next = (next % kModes + kModes) % kModes;
  s_instrumentMode = static_cast<uint8_t>(next);
  drawDisplayStatus(nullptr, nullptr);
}

static void playInstrumentAt(int16_t x, int16_t y) {
  constexpr int cx = LCD_WIDTH / 2;
  constexpr int cy = LCD_HEIGHT / 2;
  const int dx = static_cast<int>(x) - cx;
  const int dy = static_cast<int>(y) - cy;
  float angle = atan2f(static_cast<float>(dy), static_cast<float>(dx)) + 1.5707963f;
  if (angle < 0.f) {
    angle += 6.2831853f;
  }
  const int zone = static_cast<int>(floorf(angle * 8.f / 6.2831853f)) % 8;
  static constexpr int kMajorPentatonic[8] = {0, 2, 4, 7, 9, 12, 14, 16};
  int note = kMajorPentatonic[zone];
  if (s_instrumentMode == 1) {
    note += 12;
  } else if (s_instrumentMode == 2) {
    note -= 12;
  }
  const int dist2 = dx * dx + dy * dy;
  const int maxR = (LCD_WIDTH < LCD_HEIGHT ? LCD_WIDTH : LCD_HEIGHT) / 2;
  const uint8_t velocity = static_cast<uint8_t>(std::max(80, std::min(230, 80 + dist2 * 150 / (maxR * maxR))));
  astrolabe_instrument_note_on(note, velocity);
  s_lastInstrumentNote = zone;
  s_lastInstrumentNoteMs = millis();
  if (takeMutex(&s_displayMutex, 10)) {
    drawInstrumentFaceLocked();
    giveMutex(s_displayMutex);
  }
}

static void togglePlaybackFromTap() {
  auto handler = s_transportHandler;
  if (!handler) {
    return;
  }
  const bool nextPaused = !s_spotifyPaused.load();
  handler->setPause(nextPaused);
  drawDisplayStatus(nextPaused ? "Paused" : "Playing", astrolabeDeviceName());
}

static void bringSpotifyToDevice() {
  auto handler = s_transportHandler;
  if (!handler) {
    drawDisplayStatus("Spotify offline", astrolabeDeviceName());
    return;
  }
  const bool ok = handler->activatePlayback();
  drawDisplayStatus(ok ? "Transfer Spotify" : "Bring failed", astrolabeDeviceName());
}

static bool visualizerShouldUseMic() {
  return s_displayFace == DisplayFace::Visualizer && !s_spotifyActiveHere.load();
}

static void micVisualizerTask(void *) {
  bool micReady = false;
  const size_t frameSamples = pm_mic_frame_samples();
  const int channels = std::max(1, pm_mic_i2s_channels());
  std::vector<int16_t> interleaved(frameSamples * static_cast<size_t>(channels));
  std::vector<int16_t> mono(frameSamples);

  while (true) {
    if (!visualizerShouldUseMic()) {
      if (micReady) {
        pm_mic_stop();
        micReady = false;
        astrolabe_audio_visualizer_reset();
        ESP_LOGI(TAG, "mic visualizer stopped");
      }
      vTaskDelay(pdMS_TO_TICKS(180));
      continue;
    }

    if (!micReady) {
      micReady = pm_mic_begin();
      if (micReady) {
        astrolabe_audio_visualizer_reset();
        ESP_LOGI(TAG, "mic visualizer started channels=%d samples=%u", channels, (unsigned)frameSamples);
      } else {
        ESP_LOGW(TAG, "mic visualizer unavailable");
        vTaskDelay(pdMS_TO_TICKS(1000));
        continue;
      }
    }

    size_t bytesRead = 0;
    if (pm_mic_read_frame(interleaved.data(), frameSamples, &bytesRead)) {
      pm_mic_pick_channel(interleaved.data(), frameSamples, 0, mono.data());
      astrolabe_audio_visualizer_feed_output_pcm(mono.data(), frameSamples, 1);
    } else {
      ESP_LOGW(TAG, "mic visualizer read failed bytes=%u", (unsigned)bytesRead);
      pm_mic_stop();
      micReady = false;
      vTaskDelay(pdMS_TO_TICKS(300));
    }
  }
}

static void touchNavigationTask(void *) {
  Wire.end();
  delay(2);
  Wire.setPins(IIC_SDA, IIC_SCL);
  Wire.begin(IIC_SDA, IIC_SCL, 400000);
  constexpr uint32_t kLongPressMs = 650;
  constexpr uint32_t kGestureStaleMs = 1200;
  constexpr uint32_t kTouchActionCooldownMs = 350;
  bool down = false;
  int16_t x0 = 0;
  int16_t y0 = 0;
  int16_t xl = 0;
  int16_t yl = 0;
  uint32_t t0 = 0;
  uint32_t lastTouchSampleMs = 0;
  uint32_t lastActionMs = 0;
  uint32_t lastInstrumentTouchMs = 0;
  bool longFired = false;
  while (true) {
    uint8_t buf[6] = {};
    const bool ok = cst816Read(kCst816RegGesture, buf, sizeof(buf));
    const uint32_t now = millis();
    const uint8_t n = ok ? static_cast<uint8_t>(buf[1] & 0x0f) : 0;
    if (n > 0) {
      lastTouchSampleMs = now;
      const int16_t x = static_cast<int16_t>(((buf[2] & 0x0f) << 8) | buf[3]);
      const int16_t y = static_cast<int16_t>(((buf[4] & 0x0f) << 8) | buf[5]);
      if (!down) {
        down = true;
        x0 = xl = x;
        y0 = yl = y;
        t0 = now;
        longFired = false;
      } else {
        xl = x;
        yl = y;
      }
      if (s_displayFace == DisplayFace::Instrument && now - lastInstrumentTouchMs >= 115u) {
        lastInstrumentTouchMs = now;
        playInstrumentAt(xl, yl);
      }
      const int dx = static_cast<int>(xl) - x0;
      const int dy = static_cast<int>(yl) - y0;
      const int adx = dx < 0 ? -dx : dx;
      const int ady = dy < 0 ? -dy : dy;
      if (s_displayFace != DisplayFace::Instrument && !longFired && now - t0 >= kLongPressMs &&
          now - lastActionMs >= kTouchActionCooldownMs && adx <= 36 &&
          ady <= 36) {
        longFired = true;
        lastActionMs = now;
        bringSpotifyToDevice();
      }
    } else if (down) {
      down = false;
      if (longFired) {
        continue;
      }
      if (now - lastTouchSampleMs > kGestureStaleMs) {
        continue;
      }
      const int dx = static_cast<int>(xl) - x0;
      const int dy = static_cast<int>(yl) - y0;
      const int adx = dx < 0 ? -dx : dx;
      const int ady = dy < 0 ? -dy : dy;
      const uint32_t dt = now - t0;
      if (now - lastActionMs < kTouchActionCooldownMs) {
        continue;
      }
      if (dt <= 800u && (adx > 48 || ady > 48)) {
        lastActionMs = now;
        if (adx > ady + 12) {
          changeDisplayFace(dx > 0 ? 1 : -1);
        } else if (ady > adx + 12) {
          if (s_displayFace == DisplayFace::Visualizer) {
            changeVisualizerMode(dy > 0 ? 1 : -1);
          } else if (s_displayFace == DisplayFace::Instrument) {
            changeInstrumentMode(dy > 0 ? 1 : -1);
          }
        }
      } else if (s_displayFace != DisplayFace::Instrument && dt >= kLongPressMs && dt <= 2200u && adx <= 36 &&
                 ady <= 36) {
        lastActionMs = now;
        bringSpotifyToDevice();
      }
    }
    vTaskDelay(pdMS_TO_TICKS(35));
  }
}

static void playbackButtonTask(void *) {
  pinMode(ASTROLABE_CSPOT_BUTTON_PIN, INPUT_PULLUP);
  constexpr uint32_t kDebounceMs = 35;
  constexpr uint32_t kLongPressMs = 700;
  constexpr uint32_t kMaxShortPressMs = 650;
  bool lastRaw = digitalRead(ASTROLABE_CSPOT_BUTTON_PIN) == LOW;
  bool stableDown = lastRaw;
  uint32_t lastChangeMs = millis();
  uint32_t pressStartMs = stableDown ? lastChangeMs : 0;
  bool longFired = false;
  while (true) {
    const bool rawDown = digitalRead(ASTROLABE_CSPOT_BUTTON_PIN) == LOW;
    const uint32_t now = millis();
    if (rawDown != lastRaw) {
      lastRaw = rawDown;
      lastChangeMs = now;
    }
    if (rawDown != stableDown && now - lastChangeMs >= kDebounceMs) {
      stableDown = rawDown;
      if (stableDown) {
        pressStartMs = now;
        longFired = false;
      } else {
        const uint32_t heldMs = now - pressStartMs;
        if (!longFired && heldMs <= kMaxShortPressMs) {
          ESP_LOGI(TAG, "playback button short press");
          togglePlaybackFromTap();
        }
      }
    }
    if (stableDown && !longFired && now - pressStartMs >= kLongPressMs) {
      longFired = true;
      ESP_LOGI(TAG, "playback button long press");
      bringSpotifyToDevice();
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

static void updateTrackFace(const cspot::TrackInfo &track) {
  ESP_LOGI(TAG, "track face title='%s' artist='%s' album='%s' art='%s'", track.name.c_str(), track.artist.c_str(),
           track.album.c_str(), track.imageUrl.c_str());
  const bool newArtUrl = !track.imageUrl.empty() && strcmp(track.imageUrl.c_str(), s_albumArtUrl) != 0;
  bool changed = false;
  if (takeMutex(&s_faceMutex, 100)) {
    const char *nextTitle = track.name.empty() ? "Spotify" : track.name.c_str();
    const char *nextArtist = track.artist.empty() ? track.album.c_str() : track.artist.c_str();
    changed = strcmp(s_face.title, nextTitle) != 0 || strcmp(s_face.artist, nextArtist) != 0 ||
              strcmp(s_face.album, track.album.c_str()) != 0 || strcmp(s_face.artUrl, track.imageUrl.c_str()) != 0;
    copyTrunc(s_face.state, sizeof(s_face.state), "Playing");
    copyTrunc(s_face.title, sizeof(s_face.title), nextTitle);
    copyTrunc(s_face.artist, sizeof(s_face.artist), nextArtist);
    copyTrunc(s_face.album, sizeof(s_face.album), track.album);
    copyTrunc(s_face.artUrl, sizeof(s_face.artUrl), track.imageUrl);
    s_face.durationMs = track.duration > 0 ? track.duration : 210000;
    s_face.progressMs = 0;
    s_face.lastTickMs = millis();
    s_face.playing = true;
    s_face.primary = hashColor(s_face.title, 0);
    s_face.accent = hashColor(s_face.title, 1);
    giveMutex(s_faceMutex);
  }
  scheduleAlbumArt(track.imageUrl.c_str());
  if (s_displayFace == DisplayFace::Vinyl && !changed && !newArtUrl) {
    return;
  }
  if (s_displayFace == DisplayFace::Vinyl) {
    markVinylDirty(changed ? "track metadata changed" : "track art url changed");
  }
  if (s_displayFace == DisplayFace::Vinyl && newArtUrl) {
    return;
  }
  drawDisplayStatus(nullptr, nullptr);
}

static void faceAnimationTask(void *) {
  while (true) {
    bool playing = false;
    if (takeMutex(&s_faceMutex, 20)) {
      playing = s_face.playing;
      if (playing) {
        const uint32_t now = millis();
        const uint32_t dt = s_face.lastTickMs == 0 ? 0 : now - s_face.lastTickMs;
        s_face.lastTickMs = now;
        s_face.progressMs = (s_face.progressMs + dt) % std::max<uint32_t>(1, s_face.durationMs);
      }
      giveMutex(s_faceMutex);
    }
    if (playing) {
      PlayerFaceState face;
      if (takeMutex(&s_faceMutex, 10)) {
        face = s_face;
        giveMutex(s_faceMutex);
        if (s_displayFace == DisplayFace::Visualizer) {
          drawDisplayStatus(nullptr, nullptr);
        }
      }
    }
	    vTaskDelay(pdMS_TO_TICKS(s_displayFace == DisplayFace::Visualizer ? 260 : 180));
	  }
	}

static void initAstrolabeDisplay() {
  prepareWaveshareDisplayPower();
  s_displayBus = new Arduino_ESP32QSPI(LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3);
  s_panel = new Arduino_ST77916(s_displayBus, LCD_RESET, 0, true, LCD_WIDTH, LCD_HEIGHT, 0, 0, 0, 0);
  if (!s_panel || !s_panel->begin()) {
    ESP_LOGW(TAG, "display init failed");
    return;
  }
#if ASTROLABE_WAVESHARE_S3_185_V2
  applyWaveshare185cV2PanelInit();
  s_panel->setRotation(0);
#endif
  s_display = s_panel;
  s_display->fillScreen(0x0000);
  drawDisplayStatus("Booting", "ESP-IDF receiver");
  if (xTaskCreatePinnedToCoreWithCaps(faceAnimationTask, "record_face", 16 * 1024, nullptr, 0, nullptr, 1,
                                      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
    ESP_LOGW(TAG, "record face task create failed");
  }
  if (xTaskCreatePinnedToCoreWithCaps(micVisualizerTask, "mic_vis", 6 * 1024, nullptr, 1, nullptr, 1,
                                      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
    ESP_LOGW(TAG, "mic visualizer task create failed");
  }
  if (xTaskCreatePinnedToCoreWithCaps(touchNavigationTask, "touch_nav", 6 * 1024, nullptr, 1, nullptr, 1,
                                      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
    ESP_LOGW(TAG, "touch task create failed");
  }
  if (xTaskCreatePinnedToCoreWithCaps(playbackButtonTask, "play_button", 3 * 1024, nullptr, 2, nullptr, 1,
                                      MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) != pdPASS) {
    ESP_LOGW(TAG, "play button task create failed");
  }
}
#else
static void drawDisplayStatus(const char *, const char * = nullptr) {}
#endif

class AstrolabeCspotPlayer : public bell::Task {
 public:
  explicit AstrolabeCspotPlayer(std::shared_ptr<cspot::SpircHandler> handler)
      : bell::Task("cspot_player", 8 * 1024, 4, 0), handler_(std::move(handler)) {
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
          s_spotifyPaused.store(paused_);
          if (!paused_) {
            s_spotifyActiveHere.store(true);
          }
          drawDisplayStatus(paused_ ? "Paused" : "Playing", astrolabeDeviceName());
          break;
        case cspot::SpircHandler::EventType::FLUSH:
        case cspot::SpircHandler::EventType::SEEK:
          s_spotifyActiveHere.store(true);
          drawDisplayStatus("Seeking", astrolabeDeviceName());
          buffer_->emptyBuffer();
          break;
        case cspot::SpircHandler::EventType::DISC:
          s_spotifyActiveHere.store(false);
          buffer_->emptyBuffer();
          drawDisplayStatus("Remote speaker", astrolabeDeviceName());
          break;
        case cspot::SpircHandler::EventType::PLAYBACK_START:
          s_spotifyPaused.store(false);
          s_spotifyActiveHere.store(true);
          drawDisplayStatus("Playing", astrolabeDeviceName());
          buffer_->emptyBuffer();
          break;
        case cspot::SpircHandler::EventType::TRACK_INFO:
          updateTrackFace(std::get<cspot::TrackInfo>(event->data));
          break;
        case cspot::SpircHandler::EventType::VOLUME:
          sink_->volumeChanged(static_cast<uint16_t>(std::get<int>(event->data)));
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
    std::vector<uint8_t> out(4096);
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

static std::string slugifyMdnsHost(const char *name, const char *fallback) {
  std::string host;
  for (const char *p = name; p && *p && host.size() < 28; ++p) {
    const unsigned char ch = static_cast<unsigned char>(*p);
    if (isalnum(ch)) {
      host.push_back(static_cast<char>(tolower(ch)));
    } else if (!host.empty() && host.back() != '-') {
      host.push_back('-');
    }
  }
  while (!host.empty() && host.back() == '-') {
    host.pop_back();
  }
  return host.empty() ? fallback : host;
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

static void configureAstrolabeAudioRole() {
  char role[24] = {};
  nvs_handle_t nvs = 0;
  if (nvs_open("mynah", NVS_READONLY, &nvs) == ESP_OK) {
    (void)(nvsGetString(nvs, "audio_role", role, sizeof(role)) || nvsGetString(nvs, "stereo_role", role, sizeof(role)) ||
           nvsGetString(nvs, "speaker_role", role, sizeof(role)));
    nvs_close(nvs);
  }
  for (char *p = role; *p; ++p) {
    *p = static_cast<char>(tolower(static_cast<unsigned char>(*p)));
  }
  AstrolabeAudioRole audioRole = AstrolabeAudioRole::Stereo;
  if (strcmp(role, "left") == 0 || strcmp(role, "l") == 0) {
    audioRole = AstrolabeAudioRole::Left;
  } else if (strcmp(role, "right") == 0 || strcmp(role, "r") == 0) {
    audioRole = AstrolabeAudioRole::Right;
  }
  astrolabe_audio_set_role(audioRole);
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

static void configureAstrolabeIdentityFromMac() {
  uint8_t mac[6] = {};
  if (esp_wifi_get_mac(WIFI_IF_STA, mac) != ESP_OK) {
    ESP_LOGW(TAG, "could not read STA MAC for device identity");
    return;
  }
  char suffix[5];
  snprintf(suffix, sizeof(suffix), "%02X%02X", mac[4], mac[5]);
  char host[32];
  snprintf(host, sizeof(host), "astrolabe-%02x%02x", mac[4], mac[5]);
  const std::string defaultName = std::string("Astrolabe ") + suffix;
  std::string configuredName;

  nvs_handle_t nvs = 0;
  char name[64] = {};
  if (nvs_open("mynah", NVS_READONLY, &nvs) == ESP_OK) {
    if (nvsGetString(nvs, "device_name", name, sizeof(name)) || nvsGetString(nvs, "spotify_name", name, sizeof(name)) ||
        nvsGetString(nvs, "name", name, sizeof(name))) {
      configuredName = name;
    }
    nvs_close(nvs);
  }

  s_astrolabeDeviceName = configuredName.empty() ? defaultName : configuredName;
  s_astrolabeMdnsHost = slugifyMdnsHost(s_astrolabeDeviceName.c_str(), host);
  ESP_LOGI(TAG, "device identity name='%s' mdns='%s' source=%s mac=%02x:%02x:%02x:%02x:%02x:%02x",
           s_astrolabeDeviceName.c_str(), s_astrolabeMdnsHost.c_str(), configuredName.empty() ? "mac" : "nvs", mac[0],
           mac[1], mac[2], mac[3], mac[4], mac[5]);
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
  drawDisplayStatus("Spotify paired", "Saving credentials");
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
  drawDisplayStatus("Pair in Spotify", deviceName.c_str());
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
	    const std::string deviceName = astrolabeDeviceName();
	    ESP_ERROR_CHECK(mdns_init());
	    mdns_hostname_set(astrolabeMdnsHost());

	    while (true) {
	      try {
	        auto blob = std::make_shared<cspot::LoginBlob>(deviceName);
	        std::string cached;
	        if (readFile(kAuthBlobPath, &cached)) {
	          ESP_LOGI(TAG, "loading cached Spotify credentials");
	          drawDisplayStatus("Loading Spotify", "Cached account");
	          blob->loadJson(cached);
	        } else {
	          blob = waitForZeroconfLogin(deviceName);
	        }

	        drawDisplayStatus("Authenticating", deviceName.c_str());
	        auto ctx = cspot::Context::createFromBlob(blob);
	        ctx->session->connectWithRandomAp();
	        ctx->config.authData = ctx->session->authenticate(blob);
	        if (ctx->config.authData.empty()) {
	          ESP_LOGE(TAG, "Spotify authentication failed");
	          drawDisplayStatus("Spotify auth failed", deviceName.c_str());
	          vTaskDelay(pdMS_TO_TICKS(5000));
	          continue;
	        }

	        const std::string persisted = ctx->getCredentialsJson();
	        if (writeFile(kAuthBlobPath, persisted)) {
	          ESP_LOGI(TAG, "stored Spotify credentials");
	        }

	        ctx->session->startTask();
	        auto handler = std::make_shared<cspot::SpircHandler>(ctx);
	        s_transportHandler = handler;
	        handler->subscribeToMercury();
	        auto player = std::make_shared<AstrolabeCspotPlayer>(handler);
	        ESP_LOGI(TAG, "Spotify Connect receiver ready heap=%u psram=%u",
	                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
	                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
	        drawDisplayStatus("Ready in Spotify", deviceName.c_str());

	        while (true) {
	          ctx->session->handlePacket();
	        }
	      } catch (const std::exception &e) {
	        ESP_LOGE(TAG, "Spotify receiver exception: %s", e.what());
	        drawDisplayStatus("Spotify reconnect", e.what());
	      } catch (...) {
	        ESP_LOGE(TAG, "Spotify receiver exception");
	        drawDisplayStatus("Spotify reconnect", deviceName.c_str());
	      }
	      s_transportHandler.reset();
	      vTaskDelay(pdMS_TO_TICKS(5000));
	    }
	  }
	};

extern "C" void app_main(void) {
  astrolabe_audio_visualizer_reset();
#if defined(ASTROLABE_WAVESHARE_S3_185)
  initArduino();
  initAstrolabeDisplay();
#endif
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);
  ESP_ERROR_CHECK(initSpiffs());

  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  drawDisplayStatus("Connecting WiFi", ASTROLABE_WIFI_DEFAULT_SSID);
  ESP_ERROR_CHECK(connectWifiFromAstrolabeNvs());
  configureAstrolabeIdentityFromMac();
  configureAstrolabeAudioRole();
  drawDisplayStatus("WiFi connected", astrolabeDeviceName());

  bell::setDefaultLogger();
  ESP_LOGI(TAG, "starting native Astrolabe Spotify Connect receiver");
  static auto task = std::make_unique<CspotReceiverTask>();
  (void)task;
}
