# Astrolabe Variant Release Plan

Astrolabe releases are named by product family, firmware variant, and device
platform, for example **Astrolabe Ocarina 1.75**. The release manifest is
[`config/release_variants.csv`](../../config/release_variants.csv).

## Variant Matrix

| Product | Platform | Firmware variant | PlatformIO env | OTA channel |
|---|---:|---|---|---|
| Astrolabe Astrolabe 1.75 | 1.75 | Astrolabe | `waveshare_s3_175_astrolabe` | `astrolabe-astrolabe-175` |
| Astrolabe Lunasay 1.75 | 1.75 | Lunasay | `waveshare_s3_175_lunasay` | `astrolabe-lunasay-175` |
| Astrolabe Ocarina 1.75 | 1.75 | Ocarina | `waveshare_s3_175_ocarina` | `astrolabe-ocarina-175` |
| Astrolabe Cameo 1.75 | 1.75 | Cameo | `waveshare_s3_175_cameo` | `astrolabe-cameo-175` |
| Astrolabe Luopan 1.75 | 1.75 | Luopan | `waveshare_s3_175_luopan` | `astrolabe-luopan-175` |
| Astrolabe Enso 1.75 | 1.75 | Enso | `waveshare_s3_175_enso` | `astrolabe-enso-175` |
| Astrolabe Babel Fish 1.75 | 1.75 | BabelFish | `waveshare_s3_175_babel_fish` | `astrolabe-babel-fish-175` |
| Astrolabe Astrolabe 1.85 | 1.85 | Astrolabe | `waveshare_s3_185_astrolabe` | `astrolabe-astrolabe-185` |
| Astrolabe Smart Speaker 1.85 | 1.85 | SmartSpeaker | `waveshare_s3_185_smart_speaker` | `astrolabe-smart-speaker-185` |
| Astrolabe Cameo 1.45 | 1.45 | Cameo | `waveshare_s3_145_cameo` | `astrolabe-cameo-145` |

## Build And Flash Identity

Each release env compiles a forced firmware variant and platform string. On boot,
`pm_variant_begin()` writes these NVS keys in the `mynah` namespace:

| Key | Meaning |
|---|---|
| `variant` | Numeric firmware variant used by existing face gating. |
| `fw_variant` | Human-readable firmware variant, such as `Ocarina`. |
| `device_platform` | Hardware platform, such as `1.75` or `1.45`. |
| `ota_channel` | Update channel from the release manifest, such as `astrolabe-ocarina-175`. |

Forced release envs rewrite these keys on every boot so a flashed device knows
what it is even if old NVS data exists. The generic development env
`waveshare_s3_175` still allows changing variants from settings and serial QA.

## OTA Size Gate

Do not change partition tables as part of this plan. Active PlatformIO builds
already produce a partition table with two OTA app slots for the current 16 MB
1.75 layout; the release check verifies the actual generated table instead of
assuming that remains true.

## Developer Integration OTA

Developer firmware builds expose `Settings -> OTA` and `/ota` on the device HTTP
server. Browser uploads require either a physical tap on the OTA settings page
to arm uploads for a short window or a configured `MYNAH_OTA_UPLOAD_KEY` /
`MYNAH_REMOTE_CONTROL_KEY`.

The OTA page also includes a developer-only **Install latest integration build**
action. It downloads the current channel from GitHub Pages:

```text
https://astrolabe.castalia.institute/releases/integration/<ota_channel>/firmware.bin
```

The Pages workflow publishes those files from the `integration` branch after
running the release size gate. This is for bench/developer updates only; `main`
promotion still requires the hardware flash gate.

Run a report against existing builds:

```bash
./scripts/release-size-report.py
```

Build and report every release env:

```bash
./scripts/release-size-report.py --build
```

Build and report one release, for example Astrolabe Ocarina 1.75:

```bash
./scripts/release-size-report.py --build --env waveshare_s3_175_ocarina
```

Flash a release env with either helper:

```bash
./scripts/flash_astrolabe.sh --env waveshare_s3_175_ocarina
PIO_ENV=waveshare_s3_175_ocarina ./scripts/ci-flash.sh
```

When no env is explicitly set, the local flash helpers prefer the 1.75 native
ESP32-S3 USB-JTAG/serial port. If that device is absent and a 1.85-style WCH
single serial bridge is present, they fall back to
`waveshare_s3_185_astrolabe`.

The report compares each `firmware.bin` against:

| Budget | Source |
|---|---|
| Device flash | `flash_bytes` in `config/release_variants.csv`. |
| Active OTA slot | Decoded from the built `partitions.bin`; requires at least two `ota_*` app slots. |
| Planned OTA slot | `planned_ota_slot_bytes` in `config/release_variants.csv`. |

A variant is OTA-ready only when the firmware fits the active slot and the
planned slot budget. If a future partition change is needed, update the manifest
budget and rerun the report before enabling the partition table.
