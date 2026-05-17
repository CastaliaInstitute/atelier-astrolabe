# Mynah Pocketwatch (firmware)

PlatformIO firmware for the Waveshare **[ESP32-S3-Touch-AMOLED-1.75C](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.75C)** class board. Product notes: [`docs/design/pocketwatch.md`](../docs/design/pocketwatch.md).

## Default sketch: PocketMynah MVP

| Path | Role |
|------|------|
| [`sketches/PocketMynah/`](sketches/PocketMynah/) | **WiFi** + **NTP** hue clock faces (analog, Apocalypso, digital, Spotify, **Astrology**, **Moon**, **schedule** countdown, **Castalia** QR, **Settings** LAN config QR, **Version** build info + GitHub QR); **PWR hold** = STT, **BOOT** = replay last TTS (or CalDAV agenda on analog/digital/schedule); **voice-pipeline** with Castalia JWT. **Gestures**: swipe to change face. |
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

## Castalia auth modes

PocketMynah always sends Supabase's anon key as the `apikey` header. The
`Authorization` bearer is selected by
[`pm_castalia_auth`](sketches/PocketMynah/pm_castalia_auth.h):

- **Anonymous / not signed in**: `Authorization: Bearer <MYNAH_SUPABASE_ANON_KEY>`.
  This is the bootstrapping mode used before the watch has a Castalia session.
- **Signed in**: swipe to the
  [`Castalia` sign-in face](sketches/PocketMynah/PocketMynah.ino), scan the QR
  code, and complete Google sign-in on `castalia.institute`. The watch stores the
  returned Supabase access and refresh tokens in NVS, refreshes stale sessions in
  the background, and uses `Authorization: Bearer <Castalia JWT>` while the
  access token is valid.
- **Refresh failure / expired session**: the auth helper clears unusable session
  state and falls back to the anon bearer. User-scoped functions may then return
  `401` or empty/unconfigured data until you sign in again on the Castalia face.

Current service expectations:

| Service | Anonymous mode | Signed-in mode |
|---------|----------------|----------------|
| **Voice** (`voice-pipeline`) | Uses the anon bearer for basic anonymous voice requests when the backend allows them. | Sends the Castalia JWT, letting the pipeline identify the Castalia user and use signed-in context. A `401` is shown as "sign in on Castalia face". |
| **Commonplace** | Use only for public or anonymous flows. Do not write user-owned commonplace data with the anon bearer. | Required for user-owned commonplace reads/writes so Castalia can attach entries to the signed-in account. |
| **Calcifer** (`calcifer-status` / CalDAV agenda) | Can reach the function but has no user CalDAV configuration; expect unavailable, unconfigured, or `401` responses. | Required for personalized CalDAV countdowns and BOOT spoken agenda briefs. |

## Cycle face

The `Cycle` clock face is an on-device menstrual cycle wellness glance. The full ring maps to
one configured cycle: day 1 starts at the top anchor, colored bands mark the period estimate,
fertile/ovulation window, and luteal phase, and the bright marker shows today's position.

Setup stays local in NVS only; there is no cloud sync for cycle data. This is calendar math for
personal tracking, not a medical device or medical advice.

- Tap the face to log "period started today" from the watch's local date.
- Swipe up/down on the face to step through cycle length presets (default 28 days).
- Serial commands:
  - `cycle` or `cycle status`
  - `cycle YYYY MM DD`
  - `cycle today`
  - `cycle length N`
  - `cycle period N`
  - `cycle clear`

Partner and child birth profiles (up to 8) are editable on the **Settings** face web UI
(`/settings/family`) and stored in NVS for future synastry faces. Wi‑Fi SSID/password: `/settings/wifi`.

## Development

| Doc | Purpose |
|-----|---------|
| [`docs/BACKLOG.md`](docs/BACKLOG.md) | Roadmap |
| [`docs/WORKFLOW.md`](docs/WORKFLOW.md) | Issues → PR to **`integration`** → promote to **`main`** (build + flash) |

```bash
./scripts/cloud-agent.sh <issue#>              # Cloud agent → PR to integration
./scripts/ci-flash.sh                          # build + USB flash (self-hosted CI / local)
./scripts/promote-integration.sh --flash-ok    # integration → main after flash QA
```

## Limits (MVP)

- **HTTPS**: `WiFiClientSecure::setInsecure()` (no CA pin yet).
- **Voice response**: prefers `audioBase64` MP3; text-only `reply` is shown on screen when audio is absent.
- **HTTP body / response**: capped at ~1.5 MiB in `pm_voice.cpp`; very long TTS may fail.
- **Time**: UTC only on the watch face.
