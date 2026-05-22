# Astrolabe LVGL Web Simulator

This is the first LVGL-first simulator scaffold for the ESP-IDF UI migration. It
does not simulate the current Arduino_GFX rendering path. Instead, it links the
shared `ui/astrolabe_ui.c` module with LVGL and renders it to a browser canvas
through Emscripten.

## Run

Install and activate Emscripten, then build from the repository root:

```bash
./tools/web-sim/build.sh
./tools/web-sim/serve.sh
```

Open <http://localhost:8088/astrolabe-web-sim.html>.

If Emscripten is not installed locally but Docker or Podman is running:

```bash
./tools/web-sim/build-container.sh
./tools/web-sim/serve.sh
```

## Architecture

- `ui/astrolabe_ui.h` is the shared UI boundary intended for both device and
  browser builds.
- `ui/astrolabe_ui.c` owns LVGL objects and face state.
- `tools/web-sim/src/main.c` owns browser display/input drivers, LVGL tick
  scheduling, and the canvas flush callback.
- Future ESP-IDF integration should provide a parallel adapter that initializes
  `esp_lcd`, touch, and LVGL before calling `astrolabe_ui_init()`.

The scaffold exposes every current Astrolabe face ID in the firmware
`ClockFace` order. `Classic` and `Digital` have simple LVGL implementations;
the rest render LVGL placeholders with their face name, summary, and matching
index so routing, selection, and future porting can be verified incrementally.
Existing Arduino_GFX faces should be ported into the shared LVGL UI module one
face at a time.
