# Mynah simulation strategy

The Waveshare **ESP32-S3-Touch-AMOLED-1.75C** cannot be faithfully reproduced in QEMU (AMOLED QSPI, CST92xx touch, dual mics, AXP2101, round mask). Mynah uses a **two-layer simulator**: HAL abstraction + three validation levels.

## Architecture

```
Mynah app / faces
        │
        ▼
   Mynah HAL (include/mynah_hal/)
   ├── Display
   ├── Touch
   ├── IMU
   ├── Audio in/out
   ├── Storage
   └── Network
        │
        ├──────────────────┬──────────────────┐
        ▼                  ▼                  ▼
 devices/waveshare-1.75c  sim/host_round     sim/qemu
 (real board)             + lvgl_sdl         (IDF + mocks)
```

| Path | Level | Best for |
|------|-------|----------|
| [`sim/host_round/`](../sim/host_round/) | **1 — Host** | Face layout, gestures, BMP review |
| [`sim/lvgl_sdl/`](../sim/lvgl_sdl/) | **1 — Host** | LVGL face port (planned) |
| [`sim/qemu/`](../sim/qemu/) | **2 — QEMU** | Boot, OTA, storage, mocked APIs |
| USB + JTAG + Wi‑Fi BMP | **3 — HIL** | Display latency, touch, audio, power |

## Build flags

| Flag | Backend |
|------|---------|
| *(default)* | `devices/waveshare-1.75c/` |
| `MYNAH_SIM_HOST` | Host / SDL |
| `MYNAH_SIM_QEMU` | RAM framebuffer + scripted input |

Include paths (when enabling HAL in PlatformIO):

```
-Iinclude
-Idevices/waveshare-1.75c
-Isim/host_round
-Isim/qemu
```

## What QEMU does and does not do

**Good:** CPU, memory, ESP32-S3 peripherals, `idf.py qemu` debug, CI crash tests, partition/OTA logic with mocks.

**Poor:** CO5300 timing, capacitive touch, IMU, ES7210 mics, battery curves, Waveshare pin quirks.

Use **fake drivers** under `sim/qemu/` — not a board-accurate machine model.

## Verdict

| Question | Answer |
|----------|--------|
| Simulate the full Waveshare 1.75C in QEMU? | **No** — not faithfully. |
| Simulate Mynah firmware logic with QEMU + mocks? | **Yes.** |
| Best face iteration loop? | **Host sim** → QEMU for shell → **hardware** before release. |

## References

- [Espressif QEMU](https://github.com/espressif/qemu)
- [Waveshare 1.75C samples](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.75C)
- Hardware QA: [`.cursor/rules/hardware-qa.mdc`](../.cursor/rules/hardware-qa.mdc)
