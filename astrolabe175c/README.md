# Astrolabe 175C — Waveshare ESP32-S3 Touch AMOLED 1.75C

Native ESP-IDF target for the **[Waveshare ESP32-S3-Touch-AMOLED-1.75C](https://www.waveshare.com/esp32-s3-touch-amoled-1.75c.htm)**. This is the renamed successor to `faculty175/` and remains the round 466x466 AMOLED ESP-IDF app for the 1.75/1.75C hardware family.

This is **not** the 1.8″ board ([`faculty18/`](../faculty18/)) or M5 FacultyAtom ([`facultyatom/`](../facultyatom/)). It is the canonical round-watch implementation.

## Hardware

| Item | Spec |
|------|------|
| Board | ESP32-S3-Touch-AMOLED-1.75C |
| Display | 466×466 round AMOLED, CO5300 (QSPI) |
| Touch | CST9217 (I2C) — optional for v1 |
| Audio | ES7210 ADC (mic array) + ES8311 DAC (speaker) |
| PMU | AXP2101 (I2C) |

Reference: [waveshareteam/ESP32-S3-Touch-AMOLED-1.75C](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.75C) ESP-IDF v5.5 examples.

## UI

- Full-screen faculty bust (no footer, no chrome border)
- Transparent waveform bar overlaid on the bust while listening
- WiFi / think / speak use a minimal top banner only

## Voice

- Pipeline: Supabase `voice-pipeline` with **`face=faculty`**
- Contract: [`include/astrolabe_faculty175_face.h`](../include/astrolabe_faculty175_face.h)
- Faculty OTA channel: `astrolabe-faculty-amoled175`
- Cyber OTA channel: `astrolabe-cyber-175`
- LunaSay OTA channel: `astrolabe-lunasay-175`
- Fortun.ai OTA channel: `astrolabe-fortunai-175`

## Automatic OTA

After Wi-Fi starts, baseline builds check the signed `integration` OTA manifest after 20 seconds and
then every 60 seconds. Both `integration` and promoted `main` builds publish this baseline path.

The interval is persistent and configurable per device from the serial console (minimum 60 seconds):

```text
ota auto status
ota auto 300
ota auto off
```

The compiled fallback can also be changed for a build:

```bash
ASTROLABE175C_OTA_AUTO_INTERVAL_S=300 ./scripts/astrolabe175c_build.sh build
```

## Automatic OTA

After Wi-Fi starts, baseline builds check the signed `integration` OTA manifest after 20 seconds and
then every 60 seconds. Both `integration` and promoted `main` builds publish this baseline path.

The interval is persistent and configurable per device from the serial console (minimum 60 seconds):

```text
ota auto status
ota auto 300
ota auto off
```

The compiled fallback can also be changed for a build:

```bash
ASTROLABE175C_OTA_AUTO_INTERVAL_S=300 ./scripts/astrolabe175c_build.sh build
```

## Build

```bash
source ~/esp/esp-idf/export.sh
cp include/secrets.example.h include/secrets.local.h   # WiFi + Supabase
./scripts/astrolabe175c_build.sh build
./scripts/astrolabe175c_build.sh -p /dev/cu.usbmodem1101 flash monitor
```

To flash a local build from an Android phone over USB-C, use the installable
[Android PWA flash bridge](../docs/flasher/README.md):

```sh
./scripts/android_flash_bridge.py --build
```

The default `faculty` variant excludes the USB KM/screen faces and their
installer assets. Build the `cyber` variant to include them and force the Cyber
face profile:

```bash
ASTROLABE175C_VARIANT=cyber ./scripts/astrolabe175c_build.sh build
ASTROLABE175C_VARIANT=cyber ./scripts/astrolabe175c_build.sh -p /dev/cu.usbmodem1101 flash monitor
```

Build the Kickstarter LunaSay variant to force the LunaSay identity, face
profile, and signed LunaSay OTA channel:

```bash
ASTROLABE175C_VARIANT=lunasay ./scripts/astrolabe175c_build.sh build
ASTROLABE175C_VARIANT=lunasay ./scripts/astrolabe175c_build.sh -p /dev/cu.usbmodem1101 flash monitor
```

Build the Fortun.ai variant for fortune telling only. Fortun.ai selects the
fortune-telling profile; astrology remains exclusive to LunaSay:

```bash
ASTROLABE175C_VARIANT=fortunai ./scripts/astrolabe175c_build.sh build
ASTROLABE175C_VARIANT=fortunai ./scripts/astrolabe175c_build.sh -p /dev/cu.usbmodem1101 flash monitor
```

OTA publishing builds and signs each product variant independently. Firmware accepts
only a manifest whose `channel` and `firmware_variant` match the running build,
so a device cannot install another product variant's OTA.

Requires ESP-IDF **5.5+** (matches Waveshare 1.75C examples). Clone vendor XPowersLib if missing:

```bash
git clone --depth 1 https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.8 vendor/ESP32-S3-Touch-AMOLED-1.8
# XPowersLib path is shared via vendor/ESP32-S3-Touch-AMOLED-1.8/examples/ESP-IDF-v5.3.2/01_AXP2101/components/XPowersLib
```

Flash size: **32 MB** on the 1.75C; **16 MB** on the non-C 1.75. Match `CONFIG_ESPTOOLPY_FLASHSIZE_*` in `astrolabe175c/sdkconfig` to the chip (see `./scripts/astrolabe175c_identify.sh`).

## Storage baseline

All ESP32-S3 Astrolabe IDF targets reserve and mount:

- `voice_spool` as SPIFFS for local audio capture scratch
- `storage` as SPIFFS for runtime-owned caches and local fallback data
- `usbflash` as internal wear-levelled FAT mounted at `/usbflash`

On the 32 MB `astrolabe175c` layout, `usbflash` is the large user-facing content volume and `storage`
is the smaller app-facing runtime volume:

- `/usbflash/update/` for OTA payloads
- `/usbflash/media/bust_cache/` for Faculty bust PNGs
- `/usbflash/media/tarot/deck/466/` for tarot card PNGs
- `/usbflash/media/backgrounds/` for optional background overrides such as `pocketwatch_default.rgb565`

That keeps drag-and-drop media and OTA on FAT while leaving voice buffering and other app-owned scratch
paths on SPIFFS.

## USB MSC OTA demo

The local OTA console already accepts:

- `ota file <usbflash-relative|absolute-path> [sha256]`
- `ota usb [sha256]` using `update/astrolabe175c.bin` on `/usbflash`

Host-side demo flow:

```bash
source ~/esp/esp-idf/export.sh
./scripts/astrolabe175c_build.sh build
python3 ./scripts/msc_ota_demo.py astrolabe175c \
  --volume /Volumes/USBFLASH \
  --port /dev/tty.usbmodemAstrolabe175C1
```

That script copies the built image onto the mounted MSC volume at `update/astrolabe175c.bin`,
computes its SHA-256, and then issues `ota usb <sha256>` over the CDC console.

## Raspberry Pi keyboard, mouse, and software screen

These features are present only in the `cyber` build variant. Its `USB Screen`
face presents one composite USB device with CDC serial, the `usbflash` MSC
volume, and boot-protocol keyboard and mouse interfaces. The HID face shows a
per-boot Wi-Fi pairing code; the paired `/km` page provides a remote touchpad
and keyboard. Other faces restore the fixed USB Serial/JTAG console.

For a Raspberry Pi OS desktop that does not yet have the optional screen agent, wait for the
installer volume containing the `ASTROLABE` directory to mount and tap the HID face twice within ten seconds. Astrolabe opens a
terminal and runs the installer from the MSC volume. The first tap only arms the action, so an
ordinary accidental tap cannot type a command into the Pi.

USB NCM is disabled in this composite configuration to stay within the ESP32-S3 USB endpoint
budget. See [`../docs/raspberry-pi-km-screen.md`](../docs/raspberry-pi-km-screen.md) for pairing,
CDC commands, installer behavior, and the software-screen protocol.

## Which board do I have?

| Waveshare module | Flash | Display | I2C fingerprint | Astrolabe firmware |
|------------------|-------|---------|-----------------|-------------------|
| **1.75C** (cased) | 32 MB | 466×466 CO5300 | ES7210, no TCA9554 | [`astrolabe175c/`](.) |
| **1.75** | 16 MB | 466×466 CO5300 | ES7210, no TCA9554 | [`astrolabe175c/`](.) |
| **1.8** | 16 MB | 368×448 SH8601 | **TCA9554 @ 0x20** | [`faculty18/`](../faculty18/) |

Host-side (USB, before flash):

```bash
./scripts/astrolabe175c_identify.sh /dev/cu.usbmodem1101
```

On-device (after flash, serial monitor):

```text
qa board
```

If `TCA9554=1` or `guess=ESP32-S3-Touch-AMOLED-1.8`, stop — flash **`faculty18`**, not `astrolabe175c`.

## Related

- 1.8″ rectangular faculty: [`faculty18/`](../faculty18/)
- M5 Atom FacultyAtom: [`facultyatom/`](../facultyatom/)
- Round watch faces: [`main/`](main/)
