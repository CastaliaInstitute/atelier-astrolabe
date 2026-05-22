#include "pm_presence_locations.h"

#include <Arduino.h>
#include <cstring>

#include "pm_nvs.h"

namespace {

PmPresenceLocationDef s_catalog[kPmPresenceMaxLocations];
size_t s_catalog_count = 0;

void add_demo_locations(void) {
  if (s_catalog_count > 0) {
    return;
  }
  s_catalog[0] = {};
  s_catalog[0].slot = 1;
  s_catalog[0].beacon_id = pm_presence_location_beacon_id(1);
  snprintf(s_catalog[0].name, sizeof(s_catalog[0].name), "Desk");
  s_catalog[0].anchor_x_m = 0.f;
  s_catalog[0].anchor_y_m = 6.f;
  s_catalog[0].has_anchor = true;
  s_catalog_count = 1;
}

}  // namespace

bool pm_presence_is_location_id(uint32_t device_id) {
  return (device_id & 0xFF000000u) == kPmPresenceLocationIdBase;
}

uint32_t pm_presence_location_beacon_id(uint16_t slot) {
  return kPmPresenceLocationIdBase | (static_cast<uint32_t>(slot) & 0x00FFFFFFu);
}

void pm_presence_locations_begin(void) {
  s_catalog_count = 0;
  const uint8_t n = pm_nvs_get_u8("mynah_loc", "count", 0);
  for (uint8_t i = 0; i < n && s_catalog_count < kPmPresenceMaxLocations; ++i) {
    char key[8];
    snprintf(key, sizeof(key), "id%u", static_cast<unsigned>(i));
    const uint32_t bid = pm_nvs_get_u32("mynah_loc", key, 0);
    if (!pm_presence_is_location_id(bid)) {
      continue;
    }
    PmPresenceLocationDef &d = s_catalog[s_catalog_count++];
    d.slot = static_cast<uint16_t>(bid & 0xFFFFu);
    d.beacon_id = bid;
    snprintf(key, sizeof(key), "nm%u", static_cast<unsigned>(i));
    (void)pm_nvs_get_str("mynah_loc", key, d.name, sizeof(d.name), "");
    snprintf(key, sizeof(key), "x%u", static_cast<unsigned>(i));
    d.anchor_x_m = pm_nvs_get_float("mynah_loc", key, 0.f);
    snprintf(key, sizeof(key), "y%u", static_cast<unsigned>(i));
    d.anchor_y_m = pm_nvs_get_float("mynah_loc", key, 0.f);
    snprintf(key, sizeof(key), "fx%u", static_cast<unsigned>(i));
    d.has_anchor = pm_nvs_get_bool("mynah_loc", key, false);
  }
#ifdef ASTROLABE_QEMU
  if (s_catalog_count == 0) {
    add_demo_locations();
  }
#endif
}

size_t pm_presence_locations_count(void) { return s_catalog_count; }

const PmPresenceLocationDef *pm_presence_locations_get(size_t index) {
  if (index >= s_catalog_count) {
    return nullptr;
  }
  return &s_catalog[index];
}

const PmPresenceLocationDef *pm_presence_locations_find(uint32_t beacon_id) {
  for (size_t i = 0; i < s_catalog_count; ++i) {
    if (s_catalog[i].beacon_id == beacon_id) {
      return &s_catalog[i];
    }
  }
  return nullptr;
}
