# Astrolabe ESP-IDF Port

This directory is the opt-in ESP-IDF build path for the Astrolabe firmware. It
keeps Arduino as an IDF component for the first migration stage, so the existing
`sketches/Astrolabe` behavior can be preserved while subsystems move to native
IDF APIs.

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
gets into app compilation. It is still opt-in; the PlatformIO build remains the
release/flash baseline while the remaining IDF compile blockers are retired.

## Migration Order

1. Keep `pio run -e waveshare_s3_175` as the release/flash baseline.
2. Make this IDF build compile with Arduino compatibility enabled.
3. Move storage from `Preferences` to `nvs_flash` wrappers.
4. Move WiFi/HTTP/WebServer clients to `esp_wifi`, `esp_http_client`, and
   `esp_https_server`/`esp_http_server`.
5. Move audio, I2S, and USB/UAC to native IDF components.
6. Decide whether display/touch stay on Arduino libraries or move to
   `esp_lcd`/LVGL after behavior is stable.

Device QA remains the gate: boot, display, touch/swipes, WiFi settings, TTS,
face tour, and USB/JTAG behavior must pass before switching the default build.
