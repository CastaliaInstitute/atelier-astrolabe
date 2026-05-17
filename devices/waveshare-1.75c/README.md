# Waveshare ESP32-S3-Touch-AMOLED-1.75C

Hardware abstraction for the round **466×466** AMOLED pocketwatch board.

| File | Role |
|------|------|
| `pins.h` / `board_config.h` | Pin map and board identity |
| `display_waveshare_amoled.*` | `mynah::Display` → Arduino_GFX CO5300 canvas |
| `touch_waveshare.*` | `mynah::Touch` → CST92xx (delegates to `pm_touch_*` today) |
| `mynah_hal_waveshare.h` | `mynah::hal_*()` accessors for firmware |

## Migration from sketches

Sketches still use `pin_config.h` and inline CO5300 init. To adopt HAL:

1. Add `-Iinclude -Idevices/waveshare-1.75c` and compile `devices/waveshare-1.75c/*.cpp`.
2. After creating `Arduino_Canvas`, call `mynah::waveshare::display_instance().bind(canvas)`.
3. Replace direct `pm_touch_*` with `mynah::hal_touch()->sample(...)`.

Audio (ES7210/ES8311) remains in `lib/waveshare_board_audio/` until moved behind `mynah::AudioIn` / `AudioOut`.
