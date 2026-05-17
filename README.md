# Mynah Pocketwatch (firmware)

PlatformIO firmware for the Waveshare **[ESP32-S3-Touch-AMOLED-1.75C](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.75C)** class board. Product notes: [`docs/pocketwatch.md`](docs/pocketwatch.md). **Simulation (QEMU / host / HIL):** [`docs/simulation.md`](docs/simulation.md).

## Default sketch: PocketMynah MVP

| Path | Role |
|------|------|
| [`sketches/PocketMynah/`](sketches/PocketMynah/) | **WiFi** + **NTP** hue clock faces (analog, Apocalypso, digital, Spotify, **Astrology**, **Moon**, **schedule** countdown, **Castalia** QR); **PWR hold** = STT, **BOOT** = replay last TTS (or CalDAV agenda on analog/digital/schedule); **voice-pipeline** with Castalia JWT. **Gestures**: swipe to change face. |
| [`sketches/01_HelloWorld/`](sketches/01_HelloWorld/) | Minimal display sanity check; set `src_dir` in [`platformio.ini`](platformio.ini) to switch back. |
| [`lib/waveshare_board_audio/`](lib/waveshare_board_audio/) | Vendor **ES7210** / **ES8311** sources from the Waveshare tree (MIT / Apache-2.0). |
| [`lib/minimp3/`](lib/minimp3/) | [lieff/minimp3](https://github.com/lieff/minimp3) (public domain) for decoding TTS MP3. |
| [`platformio.ini`](platformio.ini) | GFX **1.5.0**, `lewisxhe/SensorLib` (CST92xx touch), flash/PSRAM, optional `upload_port`. |
| [`sketches/PocketMynah/pm_gesture.cpp`](sketches/PocketMynah/pm_gesture.cpp) | Software gesture + multitap on `pm_touch_sample()`; tunable `MYNAH_GESTURE_*` constants in-file. |

GFX note: CO5300 is constructed with **`false`** for the IPS argument (GFX 1.5.0 vs Waveshare’s newer GFX).

Optional: clone the full Waveshare repo into `vendor/` for LVGL demos (`vendor/` is gitignored).

### Host face preview (no hardware)

```bash
pip install pygame
python3 sim/host_round/viewer.py
python3 sim/host_round/viewer.py artifacts/qa-<face>.bmp   # after hardware screen.bmp capture
```

See [`sim/host_round/README.md`](sim/host_round/README.md) and [`devices/waveshare-1.75c/`](devices/waveshare-1.75c/) for the HAL layout.

```bash
git clone --depth 1 https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.75C.git vendor/ESP32-S3-Touch-AMOLED-1.75C
```

## Build & upload

```bash
pio run -e waveshare_s3_175
pio run -e waveshare_s3_175 -t upload
pio device monitor -e waveshare_s3_175
```

Pick the **Espressif CDC** serial device (e.g. macOS `/dev/cu.usbmodem1101`, USB **VID 303A** / **PID 1001**), or set `upload_port` / `monitor_port` in [`platformio.ini`](platformio.ini). Baud **115200**; `monitor_filters` include `esp32_exception_decoder` for backtraces.

### Serial debug bring-up

- Boot banners and `ESP_LOG*` tags print on the USB CDC port (`Serial` at 115200).
- Raise verbosity: add `-DCORE_DEBUG_LEVEL=4` (or `5`) to `build_flags` in `platformio.ini`.
- **JTAG**: same USB cable exposes ESP32-S3 native USB-JTAG/serial (303A:1001). Use `pio debug -e waveshare_s3_175` or OpenOCD + GDB from VS Code/Cursor; `debug_init_break = tbreak setup` is supported by the PlatformIO ESP32 debug target.

## Secrets

1. Copy [`include/secrets.example.h`](include/secrets.example.h) to **`include/secrets.local.h`** (gitignored).
2. Set **`MYNAH_WIFI_SSID`**, **`MYNAH_WIFI_PASSWORD`**, **`MYNAH_SUPABASE_URL`**, **`MYNAH_SUPABASE_ANON_KEY`** (same model as Android [`VoicePipelineClient.kt`](../android/app/src/main/java/institute/castalia/mynah/voice/VoicePipelineClient.kt): `Authorization: Bearer <anon>` + `apikey`).

If `secrets.local.h` is missing, the build uses the example file (empty strings): WiFi and voice calls will not work until you add a local secrets file.

## Limits (MVP)

- **HTTPS**: `WiFiClientSecure::setInsecure()` (no CA pin yet).
- **Voice response**: prefers `audioBase64` MP3; text-only `reply` is shown on screen when audio is absent.
- **HTTP body / response**: capped at ~1.5 MiB in `pm_voice.cpp`; very long TTS may fail.
- **Time**: UTC only on the watch face.
