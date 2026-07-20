# Astrolabe Faculty — Waveshare ESP32-S3 Touch AMOLED 1.8″

Native ESP-IDF target for the **[Waveshare ESP32-S3-Touch-AMOLED-1.8](https://www.waveshare.com/esp32-s3-touch-amoled-1.8.htm)**: always-on listen, **`face=faculty`** voice pipeline, full-screen faculty bust, transparent waveform overlay.

This is **not** the M5 FacultyAtom pendant (`facultyatom/`). It matches the round watch **Faculty** face semantics (`face=faculty`) in `astrolabe175c`.

## Hardware

| Item | Spec |
|------|------|
| Board | ESP32-S3-Touch-AMOLED-1.8 |
| Display | 368×448 AMOLED, SH8601 (QSPI) |
| Touch | FT3168 (I2C) — optional for v1 |
| Audio | ES8311 codec, onboard mic + speaker |
| IMU | QMI8658 (not used in v1 UI) |

Reference demo: [waveshareteam/ESP32-S3-Touch-AMOLED-1.8](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.8) (QSPI + I2S codec examples).

## UI (from FacultyAtom UX)

- Full-screen faculty bust (no footer, no chrome border)
- Transparent waveform bar overlaid on the bust while listening
- WiFi / think / speak use a minimal top banner only

## Voice

- Pipeline: Supabase `voice-pipeline` with **`face=faculty`**
- Contract: [`include/astrolabe_faculty_face.h`](../include/astrolabe_faculty_face.h)
- OTA channel: `astrolabe-faculty-amoled18`

## Build

```bash
source ~/esp/esp-idf/export.sh
cp include/secrets.example.h include/secrets.local.h   # WiFi + Supabase
./scripts/faculty18_build.sh build
./scripts/faculty18_build.sh -p /dev/cu.usbmodem101 flash monitor
```

## Status

| Layer | State |
|-------|--------|
| Listen / voice / faculty bust fetch | Ported from `facultyatom/` (`face=faculty`) |
| Display (SH8601 368×448 QSPI) | Board init + striped flush |
| Audio (ES8311 + PA GPIO46) | Waveshare pin map |
| TCA9554 display power | I2C @ 0x20 before panel init |

Flash size: **8 MB** on the Waveshare S3R8 module (`CONFIG_ESPTOOLPY_FLASHSIZE_8MB`).

## Related

- M5 Atom FacultyAtom: [`facultyatom/`](../facultyatom/) + [`astrolabe_faculty_atom_face.h`](../include/astrolabe_faculty_atom_face.h)
- Round watch target: [`astrolabe175c/`](../astrolabe175c/)
