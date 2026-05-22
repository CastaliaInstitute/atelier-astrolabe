# LVGL Browser Simulator

Issue: <https://github.com/CastaliaInstitute/astrolabe/issues/128>

Astrolabe is migrating away from the Arduino-first UI stack. The browser
simulator should therefore exercise the new LVGL UI layer directly, not the
current Arduino_GFX watch-face implementation.

## Target Shape

Device and browser builds should share a small UI module:

```c
void astrolabe_ui_init(void);
void astrolabe_ui_set_face(astrolabe_ui_face_t face);
void astrolabe_ui_tick(uint32_t elapsed_ms);
```

The platform adapter owns the hardware or host plumbing:

| Build | Adapter responsibility |
| --- | --- |
| ESP-IDF device | Initialize `esp_lcd`, touch input, LVGL display buffers, and timer/tick source. |
| Browser | Initialize Emscripten canvas, pointer input, LVGL display buffers, and animation frame loop. |

## First Milestone

The initial scaffold lives in `tools/web-sim` and renders `ui/astrolabe_ui.c` to
a 466x466 browser canvas. It intentionally starts with simple LVGL faces so the
boundary can stabilize before porting complex Arduino_GFX faces.

## Out Of Scope

- Simulating ESP32 peripherals, Wi-Fi, audio, NVS, or USB.
- Preserving Arduino_GFX as a browser target.
- Full visual parity with existing faces before the ESP-IDF/LVGL display path is
  selected.
