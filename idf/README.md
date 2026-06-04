# Astrolabe ESP-IDF Framework

This directory is the ESP-IDF migration path for the Astrolabe framework. The
target architecture is IDF centric: partitioning, OTA/recovery, networking,
storage, audio, USB, task ownership, and heap policy should be native IDF.

Arduino may remain as an IDF component during migration so existing
`sketches/Astrolabe` behavior can be preserved, but it is a compatibility layer,
not the architecture. New platform services should be written as IDF components
with narrow C/C++ interfaces that Arduino-era faces can call while they are
ported.

## Build

Install/export ESP-IDF 5.3-5.5, then from the repository root:

```bash
pio pkg install -e waveshare_s3_175
./scripts/idf_build.sh build
```

The IDF CMake project imports Arduino library sources from
`.pio/libdeps/waveshare_s3_175`. If your libraries live elsewhere:

```bash
./scripts/idf_build.sh -DASTROLABE_ARDUINO_LIBDEPS=/path/to/libdeps build
```

Current status: this path configures, resolves Arduino as an IDF component, and
gets into app compilation. The old PlatformIO build remains useful for comparing
behavior during migration, but recovery OTA and future factory layouts should be
implemented on native ESP-IDF first.

## Heap Policy

Internal RAM is the scarce resource. Framework components must make allocation
behavior explicit:

- use `heap_caps_*` directly or through small project wrappers;
- put framebuffers, decoded assets, response bodies, logs, and audio payloads in
  PSRAM when available;
- reserve internal RAM for DMA, TLS, task stacks, interrupt paths, and control
  structures;
- expose preflight checks based on both free internal heap and largest free
  internal block before TLS or image decode;
- serialize heavy network/TLS operations on low-memory devices;
- provide explicit teardown for display, audio, BLE, HTTP, and face modules;
- log heap deferrals with the subsystem name, free internal heap, largest block,
  and PSRAM free bytes.

The recovery app should be the strictest user of this policy: it should boot
cleanly, run one bounded update state machine, stream images to flash, and avoid
optional services while TLS is active.

## Migration Order

1. Make the IDF build compile with Arduino compatibility enabled.
2. Build the factory recovery app as native IDF with `esp_ota_ops`,
   `esp_https_ota` / `esp_http_client`, `esp_partition`, and rollback.
3. Move storage from `Preferences` to `nvs_flash` wrappers.
4. Move WiFi/HTTP/WebServer clients to `esp_wifi`, `esp_http_client`, and
   `esp_https_server`/`esp_http_server`.
5. Move audio, I2S, and USB/UAC to native IDF components.
6. Move display/touch toward `esp_lcd`/LVGL or thin native board drivers.
7. Retire Arduino shims from product apps once the face/runtime surfaces are
   native IDF.

Device QA remains the gate: boot, display, touch/swipes, WiFi settings, TTS,
face tour, and USB/JTAG behavior must pass before switching the default build.
