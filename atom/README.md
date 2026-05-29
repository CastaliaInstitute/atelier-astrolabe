# Astrolabe Wand — M5 AtomS3R + Atomic Voice Base

Native ESP-IDF target for the **tiny Astrolabe**: full-time speech-to-text and a single **faculty** conversation on the 128×128 Wand face (`face=wand`).

Hardware:

- [M5 AtomS3R](https://docs.m5stack.com/en/core/AtomS3R) (GC9107 0.85" display, front button)
- [Atomic Voice Base](https://docs.m5stack.com/en/atom/Atomic%20Echo%20Base) (ES8311 + MEMS mic + NS4150B speaker)

This follows the same pattern as [`sensecap/`](../sensecap/): a sibling ESP-IDF project, not the PlatformIO Waveshare sketch.

## Configure secrets

Copy repo secrets template and fill Wi‑Fi + Supabase (same as the pocket watch):

```bash
cp include/secrets.example.h include/secrets.local.h
# edit MYNAH_WIFI_SSID, MYNAH_WIFI_PASSWORD, MYNAH_SUPABASE_URL, MYNAH_SUPABASE_ANON_KEY
```

## Build and flash

Requires ESP-IDF 5.1+ with USB Serial/JTAG drivers.

```bash
source "$IDF_PATH/export.sh"
cd atom
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

Or from repo root:

```bash
./scripts/atom_build.sh build
./scripts/atom_build.sh -p /dev/ttyACM0 flash monitor
```

**Download mode:** hold the Atom reset ~2s until the green LED turns on, then flash.

## Runtime behavior

1. Boots into the Wand UI with default faculty **Einstein** (`a.einstein`).
2. **Always listens** — energy VAD captures utterances (≈400 ms–15 s) and posts PCM to Castalia `voice-pipeline` with `face=wand`.
3. Infers faculty from speech via STT→LLM routing (e.g. “ask Einstein…”) — no hard-coded faculty in the pipeline request.
4. Downloads a tiny **faculty bust** (`96×96` JPEG) from `faculty-bust` and shows it on the display.
5. Plays returned MP3 via ES8311.
6. **Button tap** resets faculty + conversation history to defaults.

Serial log tag: `atom_wand`.

## Pin map (AtomS3R + Voice Base)

| Function | GPIO |
|----------|------|
| Audio I2C SDA / SCL | 38 / 39 |
| I2S BCK / WS / DOUT / DIN | 8 / 6 / 5 / 7 |
| LCD CS / DC / RST / MOSI / SCK | 14 / 42 / 48 / 21 / 15 |
| Button | 41 |

Audio pins match [M5EchoBase](https://github.com/m5stack/M5Atomic-EchoBase) defaults for AtomS3R.

## Related

- Wand face contract: [`include/astrolabe_wand_face.h`](../include/astrolabe_wand_face.h)
- Watch face module: [`sketches/Astrolabe/faces/wand/`](../sketches/Astrolabe/faces/wand/)
