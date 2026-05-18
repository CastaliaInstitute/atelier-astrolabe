#pragma once

#include <cstddef>
#include <cstdint>

#include "pm_presence_locations.h"

/** Castalia presence manufacturer payload (company id 0xCA57 prefix added by caller). */
constexpr uint16_t kPmPresenceAdvCompanyId = 0xCA57;
constexpr uint8_t kPmPresenceAdvMagic0 = 0x41;
constexpr uint8_t kPmPresenceAdvMagic1 = 0x73;
constexpr uint8_t kPmPresenceAdvVersionV1 = 1;
constexpr uint8_t kPmPresenceAdvVersionV2 = 2;
constexpr uint8_t kPmPresenceAdvMaxReports = 3;

struct PmPresenceAdvReport {
  uint32_t device_id = 0;
  int8_t rssi_dbm = -127;
};

struct PmPresenceAdvDecoded {
  uint8_t version = 0;
  uint32_t device_id = 0;
  PmPresenceGraphNodeKind node_kind = PmPresenceGraphNodeKind::MobilePeer;
  PmPresenceAdvReport reports[kPmPresenceAdvMaxReports] = {};
  uint8_t report_count = 0;
};

/** Decode payload after optional company-id prefix (not including 0xCA57). */
bool pm_presence_adv_decode(const uint8_t *payload, size_t payload_len, PmPresenceAdvDecoded *out);

/**
 * Encode presence payload (no company id). Uses v2 when kind is Location or encode_v2 true.
 * Returns bytes written or 0 on error.
 */
size_t pm_presence_adv_encode(uint32_t device_id, PmPresenceGraphNodeKind node_kind,
                              const PmPresenceAdvReport *reports, uint8_t report_count, uint8_t *out,
                              size_t out_cap);

/** Classify a device id + decoded/implicit kind for graph/UI. */
PmPresenceGraphNodeKind pm_presence_resolve_node_kind(uint32_t device_id, PmPresenceGraphNodeKind adv_kind);
