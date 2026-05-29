# QEMU simulation (Level 2)

Use [Espressif’s QEMU fork](https://github.com/espressif/qemu) for **ESP32-S3 firmware logic** — boot, partitions, OTA, storage, mocked network — **not** faithful Waveshare AMOLED/touch/mic emulation.

## Build flag

```c
#define MYNAH_SIM_QEMU 1
```

Selects stubs in this directory via `include/mynah_hal/mynah_hal.h`:

| Component | File | Behavior |
|-----------|------|----------|
| Display | `display_framebuffer.c` | RAM RGB565 buffer; optional frame dump log |
| Touch | `touch_scripted.c` | Script file hook (CI scenarios) |
| IMU | `imu_scripted.c` | Zero / scripted samples |

## Workflow (ESP-IDF target)

1. Copy `sdkconfig.qemu.defaults` into your IDF project `sdkconfig.defaults`.
2. `idf.py set-target esp32s3`
3. `idf.py -DMYNAH_SIM_QEMU=1 build`
4. `idf.py qemu monitor`

Current **PlatformIO / Arduino** sketches do not run in QEMU yet; this tree is the contract for a future IDF shell or hybrid build.

## Frame output

Point `MYNAH_QEMU_FRAME_DUMP` at a path or call `display_instance().set_frame_dump_path()` before `flush()` to log dumps. A host viewer can watch `artifacts/qemu-frame-*.raw` (466×466 RGB565).
