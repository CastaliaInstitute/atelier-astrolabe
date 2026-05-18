#pragma once

#include <cstddef>
#include <cstdint>

/**
 * Static "location" beacons (desk, room, doorway) use the same Castalia presence
 * advertisement as watches (see pm_presence_adv.h), with node_kind=LocationAnchor.
 * device_id is typically 0xA5xxxxxx; surveyed coordinates live in NVS (building frame).
 */
enum class PmPresenceGraphNodeKind : uint8_t {
  MobilePeer = 0,
  LocationAnchor = 1,
};

constexpr uint32_t kPmPresenceLocationIdBase = 0xA5000000u;
constexpr size_t kPmPresenceMaxLocations = 8;

struct PmPresenceLocationDef {
  uint16_t slot = 0;
  uint32_t beacon_id = 0;
  char name[16] = {};
  /** Surveyed position in building/local frame (meters). */
  float anchor_x_m = 0.f;
  float anchor_y_m = 0.f;
  bool has_anchor = false;
};

bool pm_presence_is_location_id(uint32_t device_id);
uint32_t pm_presence_location_beacon_id(uint16_t slot);

void pm_presence_locations_begin(void);
size_t pm_presence_locations_count(void);
const PmPresenceLocationDef *pm_presence_locations_get(size_t index);
const PmPresenceLocationDef *pm_presence_locations_find(uint32_t beacon_id);
