#include "pm_presence.h"

#include "pm_presence_adv.h"
#include "pm_presence_graph.h"
#include "pm_presence_locations.h"

#include <Arduino.h>
#include <cmath>
#include <cstring>

#include <esp_mac.h>

#if !defined(ASTROLABE_QEMU) && __has_include(<BLEDevice.h>)
#include <BLEAdvertisedDevice.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEUtils.h>
#define PM_PRESENCE_BLE 1
#else
#define PM_PRESENCE_BLE 0
#endif

namespace {

constexpr uint8_t kMaxAdvReports = kPmPresenceAdvMaxReports;
constexpr uint32_t kPeerStaleMs = 15000;
constexpr uint32_t kScanPeriodMs = 400;
constexpr float kRssiEmaAlpha = 0.35f;

uint32_t s_self_id = 0;
PmPresencePeer s_peers[kPmPresenceMaxPeers];
size_t s_peer_count = 0;
float s_yaw_offset_deg = 0.f;
uint32_t s_last_scan_ms = 0;
float s_last_yaw_deg = 0.f;

uint32_t device_id_from_mac(const uint8_t mac[6]) {
  return (static_cast<uint32_t>(mac[3]) << 24) | (static_cast<uint32_t>(mac[4]) << 16) |
         (static_cast<uint32_t>(mac[5]) << 8) | static_cast<uint32_t>(mac[0]);
}

float peer_base_angle_deg(uint32_t device_id) {
  return static_cast<float>((device_id * 137u) % 360u);
}

int find_peer(uint32_t id) {
  for (size_t i = 0; i < s_peer_count; ++i) {
    if (s_peers[i].device_id == id) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

void upsert_peer(uint32_t id, int8_t rssi, uint32_t now_ms, PmPresenceGraphNodeKind kind) {
  if (id == 0 || id == s_self_id) {
    return;
  }
  kind = pm_presence_resolve_node_kind(id, kind);
  int idx = find_peer(id);
  if (idx < 0) {
    if (s_peer_count >= kPmPresenceMaxPeers) {
      return;
    }
    idx = static_cast<int>(s_peer_count++);
    s_peers[idx] = {};
    s_peers[idx].device_id = id;
    s_peers[idx].node_kind = kind;
    s_peers[idx].rssi_dbm = rssi;
    s_peers[idx].rssi_ema = rssi;
    s_peers[idx].angle_deg = peer_base_angle_deg(id) - s_yaw_offset_deg;
    while (s_peers[idx].angle_deg < 0.f) {
      s_peers[idx].angle_deg += 360.f;
    }
    while (s_peers[idx].angle_deg >= 360.f) {
      s_peers[idx].angle_deg -= 360.f;
    }
  } else {
    s_peers[idx].node_kind = kind;
    s_peers[idx].rssi_dbm = rssi;
    s_peers[idx].rssi_ema =
        static_cast<int8_t>(lrintf(kRssiEmaAlpha * static_cast<float>(rssi) +
                                   (1.f - kRssiEmaAlpha) * static_cast<float>(s_peers[idx].rssi_ema)));
  }
  s_peers[idx].last_seen_ms = now_ms;
}

void expire_peers(uint32_t now_ms) {
  size_t w = 0;
  for (size_t i = 0; i < s_peer_count; ++i) {
    if (now_ms - s_peers[i].last_seen_ms <= kPeerStaleMs) {
      if (w != i) {
        s_peers[w] = s_peers[i];
      }
      ++w;
    }
  }
  s_peer_count = w;
}

#if PM_PRESENCE_BLE

BLEScan *s_scan = nullptr;
bool s_ble_ready = false;

class PresenceScanCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) override {
    const uint32_t now_ms = millis();
    if (!advertisedDevice.haveManufacturerData()) {
      return;
    }
    const std::string mfg = advertisedDevice.getManufacturerData();
    if (mfg.size() < 10) {
      return;
    }
    const uint8_t *raw = reinterpret_cast<const uint8_t *>(mfg.data());
    size_t off = 0;
    if (mfg.size() >= 2 && raw[0] == static_cast<uint8_t>(kPmPresenceAdvCompanyId & 0xFF) &&
        raw[1] == static_cast<uint8_t>((kPmPresenceAdvCompanyId >> 8) & 0xFF)) {
      off = 2;
    }
    PmPresenceAdvDecoded adv = {};
    if (!pm_presence_adv_decode(raw + off, mfg.size() - off, &adv)) {
      return;
    }
    const int8_t rssi = static_cast<int8_t>(advertisedDevice.getRSSI());
    upsert_peer(adv.device_id, rssi, now_ms, adv.node_kind);
    for (uint8_t i = 0; i < adv.report_count; ++i) {
      const uint32_t other = adv.reports[i].device_id;
      if (other == 0 || other == adv.device_id || other == s_self_id) {
        continue;
      }
      const PmPresenceGraphNodeKind other_kind =
          pm_presence_resolve_node_kind(other, PmPresenceGraphNodeKind::MobilePeer);
      upsert_peer(other, adv.reports[i].rssi_dbm, now_ms, other_kind);
      pm_presence_graph_set_edge(adv.device_id, other, pm_presence_rssi_to_meters(adv.reports[i].rssi_dbm), now_ms);
    }
  }
};

static PresenceScanCallbacks s_scan_cb;

void build_adv_payload(uint8_t *out, size_t *out_len) {
  PmPresenceAdvReport ranked[kPmPresenceAdvMaxReports];
  uint8_t n = 0;
  for (size_t i = 0; i < s_peer_count && n < kPmPresenceAdvMaxReports; ++i) {
    if (s_peers[i].node_kind == PmPresenceGraphNodeKind::LocationAnchor) {
      continue;
    }
    ranked[n].device_id = s_peers[i].device_id;
    ranked[n].rssi_dbm = s_peers[i].rssi_ema;
    ++n;
  }
  for (uint8_t i = 0; i + 1 < n; ++i) {
    for (uint8_t j = i + 1; j < n; ++j) {
      if (ranked[j].rssi_dbm > ranked[i].rssi_dbm) {
        const PmPresenceAdvReport t = ranked[i];
        ranked[i] = ranked[j];
        ranked[j] = t;
      }
    }
  }
  *out_len = pm_presence_adv_encode(s_self_id, PmPresenceGraphNodeKind::MobilePeer, ranked, n, out, 32);
}

void refresh_advertisement(void) {
  uint8_t payload[32] = {};
  size_t len = 0;
  build_adv_payload(payload, &len);
  uint8_t mfg[36] = {};
  mfg[0] = static_cast<uint8_t>(kPmPresenceAdvCompanyId & 0xFF);
  mfg[1] = static_cast<uint8_t>((kPmPresenceAdvCompanyId >> 8) & 0xFF);
  if (len > sizeof(mfg) - 2) {
    len = sizeof(mfg) - 2;
  }
  memcpy(mfg + 2, payload, len);
  BLEAdvertisementData adv;
  adv.setFlags(0x06);
  adv.setManufacturerData(std::string(reinterpret_cast<char *>(mfg), len + 2));
  BLEAdvertising *const advertising = BLEDevice::getAdvertising();
  advertising->setAdvertisementData(adv);
  advertising->start();
}

#endif  // PM_PRESENCE_BLE

void qemu_seed_peers(uint32_t now_ms) {
#ifdef ASTROLABE_QEMU
  if (s_peer_count > 0) {
    return;
  }
  /** ~3 m, ~4 m, ~6 m from self; N1–N2 ~5 m (triangle for layout QA). */
  upsert_peer(0xA1B2C3D4u, -58, now_ms, PmPresenceGraphNodeKind::MobilePeer);
  upsert_peer(0x11223344u, -66, now_ms, PmPresenceGraphNodeKind::MobilePeer);
  upsert_peer(0xDEADBEEFu, -76, now_ms, PmPresenceGraphNodeKind::MobilePeer);
  upsert_peer(pm_presence_location_beacon_id(1), -62, now_ms, PmPresenceGraphNodeKind::LocationAnchor);
  pm_presence_graph_set_edge(0xA1B2C3D4u, 0x11223344u, 5.0f, now_ms);
  pm_presence_graph_set_edge(0xA1B2C3D4u, 0xDEADBEEFu, 7.0f, now_ms);
#else
  (void)now_ms;
#endif
}

}  // namespace

uint32_t pm_presence_self_id(void) { return s_self_id; }

bool pm_presence_begin(void) {
  uint8_t mac[6] = {};
  esp_read_mac(mac, ESP_MAC_BT);
  s_self_id = device_id_from_mac(mac);
  if (s_self_id == 0) {
    s_self_id = static_cast<uint32_t>(esp_random()) | 1u;
  }
  pm_presence_locations_begin();
#if PM_PRESENCE_BLE
  BLEDevice::init("Astrolabe");

  BLEServer *server = BLEDevice::createServer();
  (void)server;

  refresh_advertisement();

  s_scan = BLEDevice::getScan();
  s_scan->setAdvertisedDeviceCallbacks(&s_scan_cb, true);
  s_scan->setActiveScan(true);
  s_scan->setInterval(80);
  s_scan->setWindow(40);

  s_ble_ready = true;
  Serial.printf("presence: BLE id=%08x\n", static_cast<unsigned>(s_self_id));
  return true;
#else
  Serial.printf("presence: stub id=%08x\n", static_cast<unsigned>(s_self_id));
  return false;
#endif
}

void pm_presence_tick(uint32_t now_ms) {
  qemu_seed_peers(now_ms);

#if PM_PRESENCE_BLE
  if (s_ble_ready && s_scan && now_ms - s_last_scan_ms >= kScanPeriodMs) {
    s_last_scan_ms = now_ms;
    s_scan->start(1, false);
    refresh_advertisement();
  }
#endif
  expire_peers(now_ms);
}

void pm_presence_apply_yaw_delta(float delta_deg) {
  if (fabsf(delta_deg) < 0.02f) {
    return;
  }
  s_yaw_offset_deg += delta_deg;
  while (s_yaw_offset_deg >= 360.f) {
    s_yaw_offset_deg -= 360.f;
  }
  while (s_yaw_offset_deg < 0.f) {
    s_yaw_offset_deg += 360.f;
  }
  for (size_t i = 0; i < s_peer_count; ++i) {
    s_peers[i].angle_deg -= delta_deg;
    while (s_peers[i].angle_deg < 0.f) {
      s_peers[i].angle_deg += 360.f;
    }
    while (s_peers[i].angle_deg >= 360.f) {
      s_peers[i].angle_deg -= 360.f;
    }
  }
}

size_t pm_presence_peer_count(void) { return s_peer_count; }

const PmPresencePeer *pm_presence_peer(size_t index) {
  if (index >= s_peer_count) {
    return nullptr;
  }
  return &s_peers[index];
}

int pm_presence_rssi_ring(int8_t rssi_ema) {
  if (rssi_ema >= -55) {
    return 0;
  }
  if (rssi_ema >= -65) {
    return 1;
  }
  if (rssi_ema >= -75) {
    return 2;
  }
  if (rssi_ema >= -85) {
    return 3;
  }
  return 4;
}
