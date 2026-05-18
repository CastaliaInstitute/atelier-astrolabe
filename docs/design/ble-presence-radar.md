# BLE peer presence and radar face

Astrolabe watches discover each other over BLE, exchange smoothed RSSI observations, and show peers on a **radar** clock face. Distance is approximated from RSSI (concentric rings); bearing on the ring is refined with **gyro-integrated yaw** from an optional **6DOF** IMU (accelerometer + gyroscope). There is **no magnetometer** — heading is **relative** (pan the radar as you rotate the watch), not compass-north absolute.

## Manufacturer data (v1)

Company ID **0xCA57** (Castalia). Payload after the 16-bit company ID:

| Offset | Size | Field |
|--------|------|--------|
| 0 | 2 | Magic `0x41 0x73` (`As`) |
| 2 | 1 | Version `1` |
| 3 | 4 | Device ID (lower 32 bits of BLE MAC) |
| 7 | 1 | Report count `N` (0–3) |
| 8 | 5×N | Each report: peer `device_id` (4) + observed RSSI (1, signed dBm) |

Scanners apply an exponential moving average (EMA) to observed RSSI. Advertisers embed up to three strongest recent peers so nearby watches can compare mutual observations (rough co-localization, not trilateration).

## Radar face (force graph)

- **Center:** this device (fixed). **Nodes:** peers; appear and fade as BLE neighbors come and go.
- **Edges:** spring rest lengths in meters from RSSI:
  - **Self → peer:** our scan RSSI to that peer (e.g. ~3 m to N1, ~4 m to N2).
  - **Peer → peer:** when N1’s advertisement reports N2’s RSSI, we add an undirected edge (e.g. N1–N2 ≈ 5 m). Mutual reports are EMA-averaged.
- **Layout:** lightweight force-directed graph each frame — springs pull edge lengths toward measured ranges, repulsion separates nodes. The graph **relaxes** as edges appear, disappear, or update (dynamic force graph).
- **Display:** edges drawn between nodes; self links from center; node label shows estimated range in meters.
- **6DOF IMU:** optional gyro integrates **relative yaw**; the whole graph rotates in the body frame as you turn the watch (no magnetometer).

## Coexistence

- BLE starts at boot (advertise + passive scan).
- Scan duty cycles in the main loop; heavier work when the radar face is visible.
- Disabled in `ASTROLABE_QEMU` builds (synthetic peers for sim gate).

## Static location anchors (planned)

Fixed **location beacons** (desk, room, doorway) sit alongside mobile watches in the same force graph.

| Concept | Detail |
|---------|--------|
| **BLE id** | `0xA5000000 \| slot` — distinct from watch MAC-derived ids (`pm_presence_is_location_id`) |
| **Catalog** | NVS namespace `mynah_loc`: name + optional surveyed `(x, y)` in **building frame** (meters) |
| **Edges** | Self → location from scan RSSI; location ↔ watch when advertisements relay peer reports |
| **Layout** | Surveyed locations are **pinned** (strong spring to NVS coordinates); gyro rotation applies to **mobile** nodes only so anchors stay fixed in the building frame |
| **UI** | Square nodes + name label (e.g. `Desk`); circles remain mobile peers |

Firmware scaffold: [`pm_presence_locations.{h,cpp}`](../sketches/Astrolabe/pm_presence_locations.cpp). QEMU demo: **Desk** at (0, 6) m when catalog is empty.

**Later:** v2 manufacturer byte `node_kind=location`, Castalia hub sync of floor plans, USB provisioning of anchor coordinates.

## Future

- Align payload with Android [`MynahBleCodec`](https://github.com/CastaliaInstitute/mynah) when home Mynah presence ships.
- Accel-assisted gyro bias / tilt compensation; RSSI gradient walking refinement on-ring. (No magnetometer planned.)
