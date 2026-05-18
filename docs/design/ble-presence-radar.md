# BLE peer presence and radar face

Astrolabe watches and **location anchors** discover each other over BLE using the **same manufacturer advertisement**, exchange smoothed RSSI observations, and appear on a **radar** force-graph face. Distance is approximated from RSSI; mobile nodes rotate with **gyro-integrated yaw** (6DOF, no magnetometer). Pinned location nodes stay fixed in the building frame.

## Manufacturer data (shared: watches + locations)

Company ID **0xCA57** (Castalia). Payload after the 16-bit company ID:

### Version 1 (legacy)

| Offset | Size | Field |
|--------|------|--------|
| 0 | 2 | Magic `0x41 0x73` (`As`) |
| 2 | 1 | Version `1` |
| 3 | 4 | Device ID |
| 7 | 1 | Report count `N` (0–3) |
| 8 | 5×N | Each report: `device_id` (4) + RSSI dBm (1) |

Decoded as **mobile peer** unless `device_id` matches a location id / NVS catalog entry.

### Version 2 (current encode)

| Offset | Size | Field |
|--------|------|--------|
| 0 | 2 | Magic `0x41 0x73` |
| 2 | 1 | Version `2` |
| 3 | 4 | Device ID |
| 7 | 1 | **Node kind** `0` = watch, `1` = location anchor |
| 8 | 1 | Report count `N` (0–3) |
| 9 | 5×N | Each report: `device_id` (4) + RSSI dBm (1) |

**Location hardware** uses the same packet layout: fixed `device_id` (often `0xA5000000 | slot`), `node_kind=1`, and optional relay reports of nearby watches (same as a watch advertising heard peers).

Firmware: [`pm_presence_adv.{h,cpp}`](../sketches/Astrolabe/pm_presence_adv.cpp).

## Radar face (force graph)

- **Center:** this device (fixed).
- **Nodes:** mobile watches (circles) + location anchors (squares, name from NVS).
- **Edges:** spring rest lengths in meters from RSSI (self→node, node↔node via relayed reports).
- **Layout:** force-directed relaxation; pinned anchors at surveyed `(x, y)`; gyro rotates **mobile** nodes only.
- **Dynamic:** nodes and edges appear/disappear as BLE neighbors change.

## Location catalog

NVS namespace `mynah_loc`: maps `beacon_id` → human name + optional building-frame coordinates. See [`pm_presence_locations.{h,cpp}`](../sketches/Astrolabe/pm_presence_locations.cpp).

## Coexistence

- BLE advertise + passive scan at boot; scan cadence in main loop.
- Disabled in `ASTROLABE_QEMU` (synthetic graph for sim gate).

## Future

- Align with Android [`MynahBleCodec`](https://github.com/CastaliaInstitute/mynah) when home Mynah presence ships.
- Hub/LAN sync of floor plans and anchor provisioning.
- Accel-assisted gyro bias; RSSI gradient refinement (no magnetometer).
