#include "pm_presence_adv.h"

#include "pm_presence_locations.h"

#include <cstring>

namespace {

uint32_t read_u32_le(const uint8_t *p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

void write_u32_le(uint8_t *p, uint32_t v) {
  p[0] = static_cast<uint8_t>(v);
  p[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
  p[2] = static_cast<uint8_t>((v >> 16) & 0xFF);
  p[3] = static_cast<uint8_t>((v >> 24) & 0xFF);
}

bool decode_reports(const uint8_t *data, size_t len, size_t off, uint8_t n, PmPresenceAdvDecoded *out) {
  if (n > kPmPresenceAdvMaxReports || len < off + static_cast<size_t>(n) * 5u) {
    return false;
  }
  out->report_count = n;
  for (uint8_t i = 0; i < n; ++i) {
    const size_t ro = off + static_cast<size_t>(i) * 5u;
    out->reports[i].device_id = read_u32_le(data + ro);
    out->reports[i].rssi_dbm = static_cast<int8_t>(data[ro + 4]);
  }
  return true;
}

}  // namespace

bool pm_presence_adv_decode(const uint8_t *payload, size_t payload_len, PmPresenceAdvDecoded *out) {
  if (!payload || !out || payload_len < 8 || payload[0] != kPmPresenceAdvMagic0 || payload[1] != kPmPresenceAdvMagic1) {
    return false;
  }
  out->version = payload[2];
  out->device_id = read_u32_le(payload + 3);
  out->node_kind = PmPresenceGraphNodeKind::MobilePeer;
  out->report_count = 0;

  if (out->version == kPmPresenceAdvVersionV1) {
    const uint8_t n = payload[7];
    if (!decode_reports(payload, payload_len, 8, n, out)) {
      return false;
    }
    out->node_kind = pm_presence_resolve_node_kind(out->device_id, PmPresenceGraphNodeKind::MobilePeer);
    return true;
  }
  if (out->version == kPmPresenceAdvVersionV2) {
    if (payload_len < 9) {
      return false;
    }
    out->node_kind =
        (payload[7] == static_cast<uint8_t>(PmPresenceGraphNodeKind::LocationAnchor))
            ? PmPresenceGraphNodeKind::LocationAnchor
            : PmPresenceGraphNodeKind::MobilePeer;
    const uint8_t n = payload[8];
    if (!decode_reports(payload, payload_len, 9, n, out)) {
      return false;
    }
    out->node_kind = pm_presence_resolve_node_kind(out->device_id, out->node_kind);
    return true;
  }
  return false;
}

size_t pm_presence_adv_encode(uint32_t device_id, PmPresenceGraphNodeKind node_kind,
                              const PmPresenceAdvReport *reports, uint8_t report_count, uint8_t *out,
                              size_t out_cap) {
  if (!out || report_count > kPmPresenceAdvMaxReports) {
    return 0;
  }
  const size_t need = 9u + static_cast<size_t>(report_count) * 5u;
  if (out_cap < need) {
    return 0;
  }
  size_t o = 0;
  out[o++] = kPmPresenceAdvMagic0;
  out[o++] = kPmPresenceAdvMagic1;
  out[o++] = kPmPresenceAdvVersionV2;
  write_u32_le(out + o, device_id);
  o += 4;
  out[o++] = static_cast<uint8_t>(node_kind);
  out[o++] = report_count;
  for (uint8_t i = 0; i < report_count; ++i) {
    const PmPresenceAdvReport &r = reports[i];
    write_u32_le(out + o, r.device_id);
    o += 4;
    out[o++] = static_cast<uint8_t>(r.rssi_dbm);
  }
  return o;
}

PmPresenceGraphNodeKind pm_presence_resolve_node_kind(uint32_t device_id, PmPresenceGraphNodeKind adv_kind) {
  if (adv_kind == PmPresenceGraphNodeKind::LocationAnchor) {
    return PmPresenceGraphNodeKind::LocationAnchor;
  }
  if (pm_presence_is_location_id(device_id) || pm_presence_locations_find(device_id) != nullptr) {
    return PmPresenceGraphNodeKind::LocationAnchor;
  }
  return PmPresenceGraphNodeKind::MobilePeer;
}
