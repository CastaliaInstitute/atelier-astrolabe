# Astrolabe FacultyAtom — M5 AtomS3R + Atomic Voice Base

Native ESP-IDF target for the **FacultyAtom** pendant: full-time speech-to-text and faculty conversation on the 128×128 display (`face=faculty`).

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
cd facultyatom
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

Or from repo root:

```bash
./scripts/facultyatom_build.sh build
./scripts/facultyatom_build.sh -p /dev/ttyACM0 flash monitor
```

**Download mode:** hold the Atom reset ~2s until the green LED turns on, then flash.

## Serial console

USB Serial/JTAG at 115200 (default). From repo root:

```bash
./scripts/facultyatom_build.sh -p /dev/cu.usbmodem101 monitor
```

On macOS, list ports with `ls /dev/cu.usbmodem*`.

Log tags: `facultyatom`, `atom_voice`, `atom_listen`, `atom_faculty`. Stages use bracket prefixes: `[wifi]`, `[capture]`, `[stt]`, `[llm]`, `[tts]`.

## Display screenshot (serial)

Unlike the pocket watch HTTP `/screen.bmp`, FacultyAtom dumps the framebuffer over USB serial. While `idf.py monitor` is running in another terminal, or with the port free:

```bash
./scripts/facultyatom_screenshot.py -p /dev/cu.usbmodem101
# → artifacts/screens/facultyatom-20260529-123456.bmp
```

Interactive monitor: type `screen` or `screen.bmp` at the console prompt. Protocol:

```
screen: BEGIN w=128 h=128 bytes=49206
<49206 bytes raw BMP>
screen: END
```

BMP files match the framebuffer sent to the 128×128 LCD.

**Serial QA** (type at the monitor prompt, or send via pyserial):

| Command | Purpose |
|---------|---------|
| `qa help` | List QA subcommands |
| `qa status` | Heap, PSRAM, audio, WiFi RSSI |
| `qa ui` | UI state, faculty, bust status |
| `qa listen` | Passive VAD + waveform ring stats |
| `qa audio` | Active mic probe (~400 ms) + passive stats |
| `qa bust` | Faculty bust load status |
| `qa screen` | Same as `screen` (BMP dump) |
| `help` | Serial + QA summary |

**Boot / WiFi**

```
I facultyatom: [boot] Astrolabe FacultyAtom — M5 AtomS3R + Atomic Voice Base
I facultyatom: [wifi] connected ip=192.168.1.42 gw=192.168.1.1
I facultyatom: [ready] wifi ok rssi=-52 ch=6
I facultyatom: [ready] listening — speak to run STT->LLM->TTS
```

## Runtime behavior

1. Boots with default faculty **Charles Darwin** (`a.darwin`).
2. **Always listens** — energy VAD captures utterances and posts PCM to Castalia `voice-pipeline` with `face=faculty`.
3. Infers faculty from speech via STT→LLM routing.
4. Downloads a **faculty bust** (128×128) and keeps the display on a clean faculty portrait while listening.
5. Plays returned MP3 via ES8311.
6. **Button tap** resets faculty + conversation history to defaults.

## Pin map (AtomS3R + Voice Base)

| Function | GPIO |
|----------|------|
| Audio I2C SDA / SCL | 38 / 39 |
| I2S BCK / WS / DOUT / DIN | 8 / 6 / 5 / 7 |
| LCD CS / DC / RST / MOSI / SCK | 14 / 42 / 48 / 21 / 15 |
| System I2C SDA / SCL (LP5562 backlight) | 45 / 0 |
| Button | 41 |

## Related

- FacultyAtom contract: [`include/astrolabe_faculty_atom_face.h`](../include/astrolabe_faculty_atom_face.h)
- Waveshare 1.8″ faculty: [`faculty18/`](../faculty18/)
- Round watch Wand face: [`sketches/Astrolabe/faces/wand/`](../sketches/Astrolabe/faces/wand/)
