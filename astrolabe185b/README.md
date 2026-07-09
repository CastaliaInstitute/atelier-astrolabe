# Astrolabe 185B — Waveshare ESP32-S3 Touch LCD 1.85B

Native ESP-IDF target scaffold for the **[Waveshare ESP32-S3-Touch-LCD-1.85B](https://docs.waveshare.com/ESP32-S3-Touch-LCD-1.85B)**.

This directory is derived from [`astrolabe175c/`](../astrolabe175c/) to establish a dedicated ESP-IDF product path for the 1.85B port.

## Storage baseline

All ESP32-S3 Astrolabe IDF targets now reserve and mount:

- `voice_spool` as SPIFFS for local audio capture scratch
- `storage` as SPIFFS for seeded/fallback app assets
- `usbflash` as internal wear-levelled FAT mounted at `/usbflash`

On the 1.85B, removable assets can now ladder:

1. `/sdcard/...`
2. `/usbflash/...`
3. legacy SPIFFS fallback paths

The current intent is:

- keep `astrolabe175c/` as the 1.75/1.75C round AMOLED path
- port Astrolabe behavior into `astrolabe185b/` for the 1.85B rectangular LCD board
- replace the inherited 1.75C board configuration with 1.85B-specific display, touch, storage, and USB behavior

Until that board bring-up is finished, treat this as a scaffold rather than a production build target.

## USB MSC OTA demo

The local OTA console already accepts:

- `ota file <usbflash-relative|absolute-path> [sha256]`
- `ota usb [sha256]` using `update/astrolabe185b.bin` on `/usbflash`

Host-side demo flow:

```bash
source ~/esp/esp-idf/export.sh
./scripts/astrolabe185b_build.sh build
python3 ./scripts/msc_ota_demo.py astrolabe185b \
  --volume /Volumes/USBFLASH \
  --port /dev/tty.usbmodemAstrolabe185B1
```

That script copies the built image onto the mounted MSC volume at `update/astrolabe185b.bin`,
computes its SHA-256, and then issues `ota usb <sha256>` over the CDC console.
