# LVGL + SDL host simulator (Level 1 — planned)

Target stack for **visual face development** without hardware:

- LVGL 9.x with `lv_sdl` window driver
- 466×466 logical resolution, circular clip mask
- Mouse → `mynah::Touch`, keyboard for debug

## Status

**Scaffold only.** Firmware today uses **Arduino_GFX**, not LVGL. Recommended path:

1. Port shared draw helpers from `sketches/Astrolabe/faces/shared/` to LVGL widgets or a thin canvas adapter.
2. Add `sim/lvgl_sdl/CMakeLists.txt` linking LVGL + SDL2.
3. Compile face modules against `MYNAH_SIM_HOST` and `mynah::hal_display()`.

## Expected layout (when implemented)

```
sim/lvgl_sdl/
  CMakeLists.txt
  main.c
  display_sdl.c      # lv_display_t → mynah::Display
  touch_mouse.c      # SDL events → mynah::Touch
```

## Alternatives (usable now)

| Tool | Path |
|------|------|
| Pygame round viewer | [`../host_round/viewer.py`](../host_round/viewer.py) |
| Hardware BMP QA | `curl http://<watch>/screen.bmp` → viewer |
| Web prototype | Future `sim/web/` Canvas 466×466 |

See [`docs/simulation.md`](../../docs/simulation.md) for the three-level strategy.
