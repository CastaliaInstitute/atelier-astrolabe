#include "pm_colmi_r02.h"

#include <Arduino.h>

#if defined(ASTROLABE_COLMI_R02_HID_ENABLED) && !defined(ASTROLABE_QEMU) && __has_include(<BLEDevice.h>)
#include <BLEAdvertisedDevice.h>
#include <BLEClient.h>
#include <BLEDevice.h>
#include <BLERemoteCharacteristic.h>
#include <BLERemoteService.h>
#include <BLEScan.h>
#include <BLEUtils.h>

#define PM_COLMI_R02_BLE 1
#else
#define PM_COLMI_R02_BLE 0
#endif

namespace {

#if PM_COLMI_R02_BLE

static BLEUUID kRxtxService("6e40fff0-b5a3-f393-e0a9-e50e24dcca9e");
static BLEUUID kRxtxWrite("6e400002-b5a3-f393-e0a9-e50e24dcca9e");
static BLEUUID kRxtxNotify("6e400003-b5a3-f393-e0a9-e50e24dcca9e");
static BLEUUID kMainService("de5bf728-d711-4e47-af26-65e3012a5dc7");
static BLEUUID kMainWrite("de5bf72a-d711-4e47-af26-65e3012a5dc7");
static BLEUUID kMainNotify("de5bf729-d711-4e47-af26-65e3012a5dc7");

constexpr uint32_t kScanPeriodMs = 4000;
constexpr uint32_t kSampleStaleMs = 500;
constexpr uint32_t kStreamRefreshMs = 10000;
constexpr float kRawToG = 1.f / 512.f;

BLEClient *s_client = nullptr;
BLERemoteCharacteristic *s_rxtx_write = nullptr;
BLERemoteCharacteristic *s_main_write = nullptr;
uint32_t s_last_scan_ms = 0;
uint32_t s_last_stream_cmd_ms = 0;
uint32_t s_last_sample_ms = 0;
bool s_started = false;
bool s_ble_init = false;
bool s_connected = false;
bool s_streaming = false;
char s_status[28] = "ring idle";
PmColmiR02AccelSample s_last_sample;

uint8_t checksum(const uint8_t *data, size_t len_without_crc) {
  uint16_t sum = 0;
  for (size_t i = 0; i < len_without_crc; ++i) {
    sum += data[i];
  }
  return static_cast<uint8_t>(sum & 0xFF);
}

void make_command(uint8_t cmd, uint8_t a = 0, uint8_t b = 0, uint8_t *out = nullptr) {
  if (!out) {
    return;
  }
  memset(out, 0, 16);
  out[0] = cmd;
  out[1] = a;
  out[2] = b;
  out[15] = checksum(out, 15);
}

int16_t parse_i12(uint8_t hi, uint8_t lo_nibble) {
  int16_t v = static_cast<int16_t>((static_cast<uint16_t>(hi) << 4) | (lo_nibble & 0x0F));
  if (v & 0x0800) {
    v -= 0x1000;
  }
  return v;
}

void parse_notify(const uint8_t *data, size_t len) {
  if (!data || len < 10 || data[0] != 0xA1 || data[1] != 0x03) {
    return;
  }
  const int16_t raw_y = parse_i12(data[2], data[3]);
  const int16_t raw_z = parse_i12(data[4], data[5]);
  const int16_t raw_x = parse_i12(data[6], data[7]);
  s_last_sample.raw_x = raw_x;
  s_last_sample.raw_y = raw_y;
  s_last_sample.raw_z = raw_z;
  s_last_sample.x_g = static_cast<float>(raw_x) * kRawToG;
  s_last_sample.y_g = static_cast<float>(raw_y) * kRawToG;
  s_last_sample.z_g = static_cast<float>(raw_z) * kRawToG;
  s_last_sample_ms = millis();
  s_streaming = true;
  strncpy(s_status, "ring streaming", sizeof(s_status) - 1);
  s_status[sizeof(s_status) - 1] = '\0';
}

void notify_cb(BLERemoteCharacteristic *, uint8_t *data, size_t len, bool) {
  parse_notify(data, len);
}

bool send_command(BLERemoteCharacteristic *ch, const uint8_t *data, size_t len) {
  if (!ch || !data || len == 0) {
    return false;
  }
  ch->writeValue(const_cast<uint8_t *>(data), len, false);
  return true;
}

void send_raw_sensor_enable(void) {
  uint8_t cmd[16];
  make_command(0xA1, 0x04, 0x00, cmd);
  if (send_command(s_rxtx_write, cmd, sizeof(cmd)) || send_command(s_main_write, cmd, sizeof(cmd))) {
    s_last_stream_cmd_ms = millis();
    strncpy(s_status, "ring raw on", sizeof(s_status) - 1);
    s_status[sizeof(s_status) - 1] = '\0';
  }
}

void send_raw_sensor_disable(void) {
  uint8_t cmd[16];
  make_command(0xA1, 0x02, 0x00, cmd);
  (void)send_command(s_rxtx_write, cmd, sizeof(cmd));
  (void)send_command(s_main_write, cmd, sizeof(cmd));
}

bool subscribe(BLERemoteService *svc, BLEUUID uuid) {
  if (!svc) {
    return false;
  }
  BLERemoteCharacteristic *ch = svc->getCharacteristic(uuid);
  if (!ch || !ch->canNotify()) {
    return false;
  }
  ch->registerForNotify(notify_cb);
  return true;
}

bool connect_to(BLEAdvertisedDevice &dev) {
  if (s_client) {
    delete s_client;
    s_client = nullptr;
  }
  s_rxtx_write = nullptr;
  s_main_write = nullptr;
  s_client = BLEDevice::createClient();
  if (!s_client || !s_client->connect(&dev)) {
    strncpy(s_status, "ring connect fail", sizeof(s_status) - 1);
    s_status[sizeof(s_status) - 1] = '\0';
    return false;
  }

  BLERemoteService *rxtx = s_client->getService(kRxtxService);
  BLERemoteService *main = s_client->getService(kMainService);
  if (rxtx) {
    s_rxtx_write = rxtx->getCharacteristic(kRxtxWrite);
    (void)subscribe(rxtx, kRxtxNotify);
  }
  if (main) {
    s_main_write = main->getCharacteristic(kMainWrite);
    (void)subscribe(main, kMainNotify);
  }
  if (!s_rxtx_write && !s_main_write) {
    s_client->disconnect();
    strncpy(s_status, "ring no chars", sizeof(s_status) - 1);
    s_status[sizeof(s_status) - 1] = '\0';
    return false;
  }

  s_connected = true;
  s_streaming = false;
  strncpy(s_status, "ring connected", sizeof(s_status) - 1);
  s_status[sizeof(s_status) - 1] = '\0';

  uint8_t units[16];
  make_command(0x0A, 0x02, 0x00, units);
  (void)send_command(s_rxtx_write, units, sizeof(units));
  send_raw_sensor_enable();
  return true;
}

bool advertises_colmi(BLEAdvertisedDevice &dev) {
  return dev.isAdvertisingService(kRxtxService) || dev.isAdvertisingService(kMainService);
}

void scan_and_connect(uint32_t now_ms) {
  if (now_ms - s_last_scan_ms < kScanPeriodMs) {
    return;
  }
  s_last_scan_ms = now_ms;
  strncpy(s_status, "ring scanning", sizeof(s_status) - 1);
  s_status[sizeof(s_status) - 1] = '\0';

  BLEScan *scan = BLEDevice::getScan();
  if (!scan) {
    strncpy(s_status, "ring scan fail", sizeof(s_status) - 1);
    s_status[sizeof(s_status) - 1] = '\0';
    return;
  }
  scan->setActiveScan(true);
  BLEScanResults found = scan->start(1, false);
  for (int i = 0; i < found.getCount(); ++i) {
    BLEAdvertisedDevice dev = found.getDevice(i);
    if (advertises_colmi(dev)) {
      (void)connect_to(dev);
      break;
    }
  }
  scan->clearResults();
}

#endif

}  // namespace

bool pm_colmi_r02_begin(void) {
#if PM_COLMI_R02_BLE
  s_started = true;
  if (!s_ble_init) {
    BLEDevice::init("astrolabe");
    s_ble_init = true;
  }
  strncpy(s_status, "ring ready", sizeof(s_status) - 1);
  s_status[sizeof(s_status) - 1] = '\0';
  return true;
#else
  return false;
#endif
}

void pm_colmi_r02_stop(void) {
#if PM_COLMI_R02_BLE
  s_started = false;
  s_streaming = false;
  s_connected = false;
  send_raw_sensor_disable();
  if (s_client) {
    s_client->disconnect();
  }
  strncpy(s_status, "ring stopped", sizeof(s_status) - 1);
  s_status[sizeof(s_status) - 1] = '\0';
#endif
}

void pm_colmi_r02_tick(uint32_t now_ms) {
#if PM_COLMI_R02_BLE
  if (!s_started) {
    return;
  }
  if (!s_client || !s_client->isConnected()) {
    s_connected = false;
    s_streaming = false;
    scan_and_connect(now_ms);
    return;
  }
  s_connected = true;
  if (now_ms - s_last_stream_cmd_ms > kStreamRefreshMs) {
    send_raw_sensor_enable();
  }
  if (s_streaming && now_ms - s_last_sample_ms > kSampleStaleMs) {
    s_streaming = false;
    strncpy(s_status, "ring stale", sizeof(s_status) - 1);
    s_status[sizeof(s_status) - 1] = '\0';
  }
#else
  (void)now_ms;
#endif
}

bool pm_colmi_r02_ready(void) {
#if PM_COLMI_R02_BLE
  return s_connected;
#else
  return false;
#endif
}

bool pm_colmi_r02_streaming(void) {
#if PM_COLMI_R02_BLE
  return s_connected && s_streaming;
#else
  return false;
#endif
}

bool pm_colmi_r02_accel_g(PmColmiR02AccelSample *out) {
#if PM_COLMI_R02_BLE
  if (!out || !s_streaming) {
    return false;
  }
  *out = s_last_sample;
  out->age_ms = millis() - s_last_sample_ms;
  return out->age_ms <= kSampleStaleMs;
#else
  (void)out;
  return false;
#endif
}

const char *pm_colmi_r02_status_label(void) {
#if PM_COLMI_R02_BLE
  return s_status;
#else
  return "ring disabled";
#endif
}
