#include "pm_colmi_r02.h"

#include <Arduino.h>
#include <cstring>
#include <string>

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
constexpr uint32_t kWellnessPollMs = 15000;
constexpr uint32_t kHistoryPollMs = 300000;
constexpr uint32_t kWellnessStaleMs = 10UL * 60UL * 1000UL;
constexpr float kRawToG = 1.f / 512.f;

constexpr uint8_t kCmdBattery = 0x03;
constexpr uint8_t kCmdRealtimeHeartRate = 0x1E;
constexpr uint8_t kCmdSyncStress = 0x37;
constexpr uint8_t kCmdSyncHrv = 0x39;
constexpr uint8_t kCmdManualRealtime = 0x69;
constexpr uint8_t kCmdBigDataV2 = 0xBC;
constexpr uint8_t kBigDataSleep = 0x27;
constexpr uint8_t kRealtimeHeartRate = 0x01;
constexpr uint8_t kRealtimeSpo2 = 0x03;
constexpr uint8_t kRealtimePressure = 0x08;
constexpr uint8_t kRealtimeHrv = 0x0A;
constexpr uint8_t kRealtimeStart = 0x01;
constexpr uint8_t kRealtimeStop = 0x04;
constexpr uint8_t kSleepLight = 0x02;
constexpr uint8_t kSleepDeep = 0x03;
constexpr uint8_t kSleepRem = 0x04;
constexpr uint8_t kSleepAwake = 0x05;

BLEClient *s_client = nullptr;
BLERemoteCharacteristic *s_rxtx_write = nullptr;
BLERemoteCharacteristic *s_main_write = nullptr;
uint32_t s_last_scan_ms = 0;
uint32_t s_last_stream_cmd_ms = 0;
uint32_t s_last_sample_ms = 0;
uint32_t s_last_wellness_poll_ms = 0;
uint32_t s_last_history_poll_ms = 0;
uint32_t s_last_wellness_ms = 0;
bool s_started = false;
bool s_ble_init = false;
bool s_connected = false;
bool s_streaming = false;
char s_status[28] = "ring idle";
PmColmiR02AccelSample s_last_sample;
PmColmiR02Wellness s_wellness;
uint8_t s_realtime_kind = kRealtimeHeartRate;

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

void make_command_payload(const uint8_t *payload, size_t payload_len, uint8_t *out) {
  if (!payload || !out) {
    return;
  }
  memset(out, 0, 16);
  const size_t n = payload_len > 15 ? 15 : payload_len;
  memcpy(out, payload, n);
  out[15] = checksum(out, 15);
}

int16_t parse_i12(uint8_t hi, uint8_t lo_nibble) {
  int16_t v = static_cast<int16_t>((static_cast<uint16_t>(hi) << 4) | (lo_nibble & 0x0F));
  if (v & 0x0800) {
    v -= 0x1000;
  }
  return v;
}

uint16_t u16le(uint8_t lo, uint8_t hi) {
  return static_cast<uint16_t>(lo) | (static_cast<uint16_t>(hi) << 8);
}

void mark_wellness(void) {
  s_last_wellness_ms = millis();
}

void parse_realtime_value(const uint8_t *data, size_t len) {
  if (!data || len < 4 || data[2] != 0x00) {
    return;
  }
  const uint8_t kind = data[1];
  const uint8_t value = data[3];
  if (value == 0) {
    return;
  }
  switch (kind) {
    case kRealtimeHeartRate:
      s_wellness.heart_rate_bpm = value;
      s_wellness.heart_rate_valid = true;
      break;
    case kRealtimeSpo2:
      s_wellness.spo2_percent = value;
      s_wellness.spo2_valid = true;
      break;
    case kRealtimePressure:
      s_wellness.stress = value;
      s_wellness.stress_valid = true;
      break;
    case kRealtimeHrv:
      s_wellness.hrv_ms = value;
      s_wellness.hrv_valid = true;
      break;
    default:
      return;
  }
  mark_wellness();
}

void parse_history_series(const uint8_t *data, size_t len, bool hrv) {
  if (!data || len < 4 || data[1] == 0xFF || data[1] == 0x00) {
    return;
  }
  const uint8_t packet_nr = data[1];
  const size_t start = packet_nr == 1 ? 3 : 2;
  for (size_t i = start; i < len - 1; ++i) {
    const uint8_t value = data[i];
    if (value == 0) {
      continue;
    }
    if (hrv) {
      s_wellness.hrv_ms = value;
      s_wellness.hrv_valid = true;
    } else {
      s_wellness.stress = value;
      s_wellness.stress_valid = true;
    }
    mark_wellness();
  }
}

void parse_sleep_history(const uint8_t *data, size_t len) {
  if (!data || len < 8) {
    return;
  }
  const uint16_t packet_len = u16le(data[2], data[3]);
  if (packet_len < 2) {
    return;
  }
  size_t index = 7;
  const uint8_t days_in_packet = data[6];
  PmColmiR02Wellness sleep = s_wellness;
  sleep.sleep_total_min = 0;
  sleep.sleep_light_min = 0;
  sleep.sleep_deep_min = 0;
  sleep.sleep_rem_min = 0;
  sleep.sleep_awake_min = 0;
  for (uint8_t day = 0; day < days_in_packet && index + 5 < len; ++day) {
    const uint8_t days_ago = data[index++];
    const uint8_t day_bytes = data[index++];
    const uint16_t sleep_start = u16le(data[index], data[index + 1]);
    index += 2;
    const uint16_t sleep_end = u16le(data[index], data[index + 1]);
    index += 2;
    (void)sleep_start;
    (void)sleep_end;
    if (days_ago != 0) {
      index += day_bytes > 4 ? day_bytes - 4 : 0;
      continue;
    }
    for (uint8_t j = 4; j + 1 < day_bytes && index + 1 < len; j += 2) {
      const uint8_t stage = data[index++];
      const uint8_t minutes = data[index++];
      if (minutes == 0) {
        continue;
      }
      sleep.sleep_total_min += minutes;
      switch (stage) {
        case kSleepLight:
          sleep.sleep_light_min += minutes;
          break;
        case kSleepDeep:
          sleep.sleep_deep_min += minutes;
          break;
        case kSleepRem:
          sleep.sleep_rem_min += minutes;
          break;
        case kSleepAwake:
          sleep.sleep_awake_min += minutes;
          break;
        default:
          break;
      }
    }
    sleep.sleep_valid = sleep.sleep_total_min > 0;
  }
  if (sleep.sleep_valid) {
    s_wellness = sleep;
    mark_wellness();
  }
}

void parse_notify(const uint8_t *data, size_t len) {
  if (!data || len < 2) {
    return;
  }
  if (len >= 4 && data[0] == kCmdBattery) {
    s_wellness.battery_percent = data[1];
    s_wellness.charging = data[2] == 0x01;
    s_wellness.battery_valid = true;
    mark_wellness();
    return;
  }
  if (len >= 4 && (data[0] == kCmdManualRealtime || data[0] == kCmdRealtimeHeartRate)) {
    parse_realtime_value(data, len);
    return;
  }
  if (data[0] == kCmdSyncStress) {
    parse_history_series(data, len, false);
    return;
  }
  if (data[0] == kCmdSyncHrv) {
    parse_history_series(data, len, true);
    return;
  }
  if (data[0] == kCmdBigDataV2 && len >= 7 && data[1] == kBigDataSleep) {
    parse_sleep_history(data, len);
    return;
  }
  if (len < 10 || data[0] != 0xA1 || data[1] != 0x03) {
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

void send_packet_both(const uint8_t *data, size_t len) {
  (void)send_command(s_rxtx_write, data, len);
  (void)send_command(s_main_write, data, len);
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

void send_wellness_history_requests(void) {
  uint8_t cmd[16];
  make_command(kCmdBattery, 0x00, 0x00, cmd);
  send_packet_both(cmd, sizeof(cmd));

  make_command(kCmdSyncStress, 0x00, 0x00, cmd);
  send_packet_both(cmd, sizeof(cmd));

  const uint8_t hrv_payload[] = {kCmdSyncHrv, 0x00, 0x00, 0x00, 0x00};
  make_command_payload(hrv_payload, sizeof(hrv_payload), cmd);
  send_packet_both(cmd, sizeof(cmd));

  const uint8_t sleep_payload[] = {kCmdBigDataV2, kBigDataSleep, 0x01, 0x00, 0xFF, 0x00, 0xFF};
  (void)send_command(s_main_write, sleep_payload, sizeof(sleep_payload));
  s_last_history_poll_ms = millis();
}

void send_realtime_probe(void) {
  uint8_t cmd[16];
  const uint8_t payload[] = {kCmdManualRealtime, s_realtime_kind, kRealtimeStart};
  make_command_payload(payload, sizeof(payload), cmd);
  send_packet_both(cmd, sizeof(cmd));

  switch (s_realtime_kind) {
    case kRealtimeHeartRate:
      s_realtime_kind = kRealtimeSpo2;
      break;
    case kRealtimeSpo2:
      s_realtime_kind = kRealtimeHrv;
      break;
    case kRealtimeHrv:
      s_realtime_kind = kRealtimePressure;
      break;
    default:
      s_realtime_kind = kRealtimeHeartRate;
      break;
  }
  s_last_wellness_poll_ms = millis();
}

void send_raw_sensor_disable(void) {
  uint8_t cmd[16];
  make_command(0xA1, 0x02, 0x00, cmd);
  (void)send_command(s_rxtx_write, cmd, sizeof(cmd));
  (void)send_command(s_main_write, cmd, sizeof(cmd));
  const uint8_t realtime_stop_payload[] = {kCmdManualRealtime, kRealtimeHeartRate, kRealtimeStop};
  make_command_payload(realtime_stop_payload, sizeof(realtime_stop_payload), cmd);
  send_packet_both(cmd, sizeof(cmd));
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
  if (dev.haveName()) {
    const std::string name = dev.getName();
    if (name.find("COLMI") != std::string::npos || name.find("R02") != std::string::npos ||
        name.find("R10") != std::string::npos) {
      return true;
    }
  }
  if (dev.haveManufacturerData()) {
    const std::string data = dev.getManufacturerData();
    if (data.size() >= 2 && static_cast<uint8_t>(data[0]) == 0xFE && static_cast<uint8_t>(data[1]) == 0xE7) {
      return true;
    }
  }
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
  if (now_ms - s_last_wellness_poll_ms > kWellnessPollMs) {
    send_realtime_probe();
  }
  if (now_ms - s_last_history_poll_ms > kHistoryPollMs) {
    send_wellness_history_requests();
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

bool pm_colmi_r02_wellness(PmColmiR02Wellness *out) {
#if PM_COLMI_R02_BLE
  if (!out || s_last_wellness_ms == 0) {
    return false;
  }
  *out = s_wellness;
  out->age_ms = millis() - s_last_wellness_ms;
  return out->age_ms <= kWellnessStaleMs;
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
