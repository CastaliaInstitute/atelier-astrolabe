#pragma once

#include <cstddef>
#include <cstdint>

#include "pm_presence_locations.h"

/** One discovered node from a Castalia presence advertisement (watch or location). */
struct PmPresencePeer {
  uint32_t device_id = 0;
  PmPresenceGraphNodeKind node_kind = PmPresenceGraphNodeKind::MobilePeer;
  int8_t rssi_dbm = -127;
  /** Smoothed RSSI used for ring placement. */
  int8_t rssi_ema = -127;
  uint32_t last_seen_ms = 0;
  /** Clockwise degrees from top (0 = 12 o'clock); adjusted by integrated gyro yaw. */
  float angle_deg = 0.f;
};

constexpr size_t kPmPresenceMaxPeers = 8;

uint32_t pm_presence_self_id(void);

/** Self id + location catalog. */
bool pm_presence_begin(void);

/** Call once in setup before WiFi (BT controller must start before WiFi for coex). */
/** Start BLE advertise + scan when Radar face opens (no-op stub on QEMU). */
bool pm_presence_ble_begin(void);
bool pm_presence_ble_is_ready(void);
bool pm_presence_ble_failed(void);

/** Start/stop scan+advertise while the Radar face is visible (avoids BLE callbacks during other faces). */
void pm_presence_ble_set_radar_active(bool active);
/** Temporarily keep BLE off when another heap-hungry flow, such as tour TTS, owns the radio heap. */
void pm_presence_ble_set_suppressed(bool suppressed);
/** Stop BLE and return controller/host heap after leaving Radar. Reinitialized on next Radar entry. */
void pm_presence_ble_end(void);

/** Demo peers for QEMU or when BLE init fails (radar UI still usable). */
void pm_presence_seed_demo_peers(uint32_t now_ms);

/** Poll scan results, refresh advertisement payload, expire stale peers. */
void pm_presence_tick(uint32_t now_ms);

/** Apply relative yaw delta from 6DOF gyro (degrees) to peer bearing on the radar ring. */
void pm_presence_apply_yaw_delta(float delta_deg);

size_t pm_presence_peer_count(void);
const PmPresencePeer *pm_presence_peer(size_t index);

/** For radar UI: map EMA RSSI to ring index 0 (near) .. 4 (far). */
int pm_presence_rssi_ring(int8_t rssi_ema);
