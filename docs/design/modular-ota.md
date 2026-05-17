# Modular OTA (PocketMynah / Astrolabe)

Runtime firmware and face experience update independently on **32 MB** Waveshare ESP32-S3-Touch-AMOLED-**1.75C** flash.

## Partition table

See [`partitions_32mb.csv`](../../partitions_32mb.csv). Summary:

| Partition | Size | Role |
|-----------|------|------|
| `ota_0` / `ota_1` | 3 MB each | Runtime A/B (ESP-IDF OTA) |
| `faces_a` / `faces_b` | 7 MB each | Face-pack LittleFS A/B |
| `assets` | 4 MB | Factory glyphs/fonts |
| `commonplace` | 5 MB | FAT journal |
| `ephemeris` | 2 MB | FAT cache |
| `crashlog` | 1 MB | FAT diagnostics |

`platformio.ini` sets `board_build.flash_size = 32MB` (hardware `esptool.py flash_id` reports 32MB; older ini had 16MB incorrectly).

## First flash after partition migration

1. `esptool.py --port /dev/cu.usbmodem* erase_flash`
2. `./scripts/flash_factory.sh`
3. Optional: `python3 tools/build_facepack.py` then flash `build/core-facepack.img` to `faces_a`

OTA cannot change the partition table in the field.

## Serial commands

| Command | Action |
|---------|--------|
| `ota status` | Runtime / face partition / pending flags |
| `safe` | Force compiled safe fallback UI |
| `face huepack` | Switch to declarative pack face (index 10) |

## NVS (`mynah_ota`)

Face active slot, pending pack, boot-attempt rollback (revert after >3 failed boots).

## Issue

GitHub [#60](https://github.com/CastaliaInstitute/astrolabe/issues/60) — branch `feature/60-modular-ota`.
