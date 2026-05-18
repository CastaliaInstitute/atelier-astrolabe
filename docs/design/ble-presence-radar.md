# BLE peer presence and radar face

Astrolabe watches discover each other over BLE, exchange smoothed RSSI observations, and show peers on a **radar** clock face. Distance is approximated from RSSI (concentric rings); bearing on the ring is refined with **IMU yaw** when a gyro is present.

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

## Radar face

- **Center:** this device.
- **Rings:** five distance bins from RSSI (−40 dBm near … −90 dBm far).
- **Blips:** one per peer; radius from EMA RSSI; angle = stable hash(device_id) minus integrated **yaw** (degrees clockwise from top).
- **IMU:** optional QMI8658 on the shared I2C bus; gyro-Z integrated when the part is detected. Waveshare 1.75C reference SKU has no IMU — the face still works; rotating the watch does not pan the radar until a gyro is present.

## Coexistence

- BLE starts at boot (advertise + passive scan).
- Scan duty cycles in the main loop; heavier work when the radar face is visible.
- Disabled in `ASTROLABE_QEMU` builds (synthetic peers for sim gate).

## Future

- Align payload with Android [`MynahBleCodec`](https://github.com/CastaliaInstitute/mynah) when home Mynah presence ships.
- Magnetometer fusion for absolute bearing; RSSI gradient walking refinement on-ring.
