# Astrolabe FacultyPaper - M5 PaperColor

Native ESP-IDF target for the M5 PaperColor: full-time speech-to-text and faculty conversation for the 600x400 color e-paper form factor (`face=faculty`).

Hardware:

- [M5 PaperColor](https://docs.m5stack.com/en/core/PaperColor) C151
- ESP32-S3R8, 16 MB flash, 8 MB PSRAM
- 4-inch Spectra 6 color e-paper, 600x400 visible after rotation
- ES8311 speaker codec + ES7210 mic ADC
- AW8737A speaker amplifier
- M5PM1 power/IO controller on the system I2C bus
- microSD slot used for PCM capture when present, with SPIFFS fallback
- microSD conversation/notes journal under `/sdcard/astrolabe/facultypaper`

This follows the same native ESP-IDF sibling-project pattern as [`facultyatom/`](../facultyatom/) and reuses the shared `astrolabe_audio_pipeline` flow.

## Configure Secrets

```bash
cp include/secrets.example.h include/secrets.local.h
# edit MYNAH_WIFI_SSID, MYNAH_WIFI_PASSWORD, MYNAH_SUPABASE_URL, MYNAH_SUPABASE_ANON_KEY
```

## Build And Flash

Requires ESP-IDF 5.5+.

```bash
source "$IDF_PATH/export.sh"
./scripts/facultypaper_build.sh build
./scripts/facultypaper_build.sh -p /dev/cu.usbmodem1101 flash monitor
```

Download mode: connect USB-C, then hold the side reset/download control per the M5 PaperColor instructions.

## Serial QA

```bash
./scripts/facultypaper_build.sh -p /dev/cu.usbmodem1101 monitor
./scripts/facultypaper_screenshot.py -p /dev/cu.usbmodem1101
```

Commands:

| Command | Purpose |
|---------|---------|
| `qa status` | Heap, PSRAM, audio, SD/storage, WiFi RSSI |
| `qa ui` | UI state, faculty, bust status |
| `qa listen` | Passive VAD state |
| `qa audio` | Active mic probe |
| `qa speaker` | Active speaker write probe with a short 440 Hz tone |
| `qa bust` | Faculty bust load status |
| `qa memory` | Conversation and note storage paths |
| `note <text>` | Append a manual note to SD as JSONL |
| `screen` | Dump a 600x400 24-bit BMP over serial |

## Runtime Behavior

1. Boots with default faculty Charles Darwin (`a.darwin`).
2. Detects M5PM1 and applies the vendor demo power/GPIO sequence best-effort: I2C sleep disabled, EPD power enabled, SD detect enabled, charge/boost enabled.
3. Mounts microSD at `/sdcard` for utterance capture; falls back to SPIFFS when no card is present.
4. Creates `/sdcard/astrolabe/facultypaper` when SD is present.
5. Appends each completed STT/LLM/TTS turn to `conversations.jsonl` and prompt history to `history.txt`.
6. Appends serial `note <text>` entries to `notes.jsonl`.
7. Downloads a 320x320 faculty bust and renders it into the 600x400 framebuffer.

Audio note: ES8311/ES7210 codec setup is wired, but current hardware QA still reports zero mic samples (`mic_probe_peak=0`). The firmware marks audio `off` when the boot probe is silent, so STT/LLM/TTS capture remains blocked until the ES7210 path is corrected.

Intended audio flow after capture is fixed:

1. Always listen with energy VAD.
2. Post captured PCM to Castalia `voice-pipeline` with `face=faculty`.
3. Receive STT transcript, faculty-routed LLM reply, TTS MP3, and updated faculty identity.
4. Play returned TTS through the onboard speaker.

Display note: the software framebuffer and serial BMP capture path are wired, but physical Spectra 6 panel refresh is still a board-driver TODO in `paper_display_flush()`.

## Pin Map

| Function | GPIO |
|----------|------|
| E-paper SCLK / MOSI / CS / DC / BUSY / RST | 15 / 13 / 44 / 43 / 11 / 12 |
| Audio I2C SCL / SDA | 2 / 3 |
| Audio I2S MCLK / LRCK / BCLK / DOUT / DIN | 42 / 41 / 40 / 39 / 38 |
| Audio power / speaker amp enable | 45 / 46 |
| microSD CS / SCLK / MOSI / MISO | 47 / 15 / 13 / 14 |
| Buttons C / B / A | 1 / 9 / 10 |
| M5PM1 GPIO0 / GPIO1 / GPIO4 | EPD_EN / SD_DEC / SD_DET_EN |

References:

- M5 PaperColor docs: https://docs.m5stack.com/en/core/PaperColor
- M5 PaperColor factory demo: https://github.com/m5stack/M5PaperColor-UserDemo
