# Mynah Pocketwatch (firmware)

PlatformIO firmware for the Waveshare **[ESP32-S3-Touch-AMOLED-1.75C](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.75C)** class board. Product notes: [`docs/design/pocketwatch.md`](../docs/design/pocketwatch.md).

## Default sketch: PocketMynah MVP

| Path | Role |
|------|------|
| [`sketches/PocketMynah/`](sketches/PocketMynah/) | **WiFi** + **NTP** (UTC) **hue clock**; **PTT** (lower rim, ~`MYNAH_PTT_ARM_MS` hold) → **voice-pipeline**; **Gestures**: tap / double / triple (single finger, proximity + timing), **swipe** (4-way), **long press**, **2–5 finger** short multitaps. Events queue for UI + `Serial` logging. |
| [`sketches/01_HelloWorld/`](sketches/01_HelloWorld/) | Minimal display sanity check; set `src_dir` in [`platformio.ini`](platformio.ini) to switch back. |
| [`lib/waveshare_board_audio/`](lib/waveshare_board_audio/) | Vendor **ES7210** / **ES8311** sources from the Waveshare tree (MIT / Apache-2.0). |
| [`lib/minimp3/`](lib/minimp3/) | [lieff/minimp3](https://github.com/lieff/minimp3) (public domain) for decoding TTS MP3. |
| [`platformio.ini`](platformio.ini) | GFX **1.5.0**, `lewisxhe/SensorLib` (CST92xx touch), flash/PSRAM, optional `upload_port`. |
| [`sketches/PocketMynah/pm_gesture.cpp`](sketches/PocketMynah/pm_gesture.cpp) | Software gesture + multitap on `pm_touch_sample()`; tunable `MYNAH_GESTURE_*` constants in-file. |

GFX note: CO5300 is constructed with **`false`** for the IPS argument (GFX 1.5.0 vs Waveshare’s newer GFX).

Optional: clone the full Waveshare repo into `vendor/` for LVGL demos (`vendor/` is gitignored).

```bash
git clone --depth 1 https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.75C.git vendor/ESP32-S3-Touch-AMOLED-1.75C
```

## Build & upload

```bash
cd pocketwatch
pio run -e waveshare_s3_175
pio run -e waveshare_s3_175 -t upload
pio device monitor -e waveshare_s3_175
```

Pick the **Espressif CDC** serial device (e.g. macOS `/dev/cu.usbmodem…`, USB **VID 303A**), or set `upload_port` under `[env:waveshare_s3_175]`.

## Secrets

1. Copy [`include/secrets.example.h`](include/secrets.example.h) to **`include/secrets.local.h`** (gitignored).
2. Set **`MYNAH_WIFI_SSID`**, **`MYNAH_WIFI_PASSWORD`**, **`MYNAH_SUPABASE_URL`**, **`MYNAH_SUPABASE_ANON_KEY`** (same model as Android [`VoicePipelineClient.kt`](../android/app/src/main/java/institute/castalia/mynah/voice/VoicePipelineClient.kt): `Authorization: Bearer <anon>` + `apikey`).

If `secrets.local.h` is missing, the build uses the example file (empty strings): WiFi and voice calls will not work until you add a local secrets file.

## Limits (MVP)

- **HTTPS**: `WiFiClientSecure::setInsecure()` (no CA pin yet).
- **Voice response**: assumes `voice-pipeline` JSON with `audioBase64` (plain `ask-faculty`-only responses are not handled here).
- **HTTP body / response**: capped (see `pm_voice.cpp`); very long TTS may fail.
- **Time**: UTC only on the watch face.
