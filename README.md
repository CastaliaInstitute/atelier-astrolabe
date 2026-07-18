# Astrolabe firmware

Native ESP-IDF firmware for the Castalia Institute Astrolabe device family.
The canonical round-pocketwatch implementation is
[`astrolabe175c/`](astrolabe175c/), targeting the Waveshare
ESP32-S3-Touch-AMOLED-1.75/1.75C hardware family.

The former Arduino/PlatformIO sketch implementation has been removed. New
round-watch product work belongs in `astrolabe175c`; do not reintroduce a second
firmware implementation.

## Projects

| Path | Hardware |
|---|---|
| [`astrolabe175c/`](astrolabe175c/) | 466×466 round AMOLED, ESP32-S3, ES7210/ES8311, AXP2101 |
| [`astrolabe185b/`](astrolabe185b/) | 1.85-inch Astrolabe target |
| [`faculty18/`](faculty18/) | 1.8-inch rectangular Faculty target |
| [`facultyatom/`](facultyatom/) | M5 Atom Faculty target |
| [`sensecap/`](sensecap/) | SenseCAP target |

Shared native components live under [`lib/`](lib/) and shared contracts under
[`include/`](include/).

## Build the round Astrolabe

ESP-IDF 5.5 or later is required.

```bash
source ~/esp/esp-idf/export.sh
cp include/secrets.example.h include/secrets.local.h
./scripts/astrolabe175c_build.sh build
```

Flash and monitor a connected device:

```bash
./scripts/astrolabe175c_build.sh -p /dev/cu.usbmodem1101 flash monitor
```

See [`astrolabe175c/README.md`](astrolabe175c/README.md) for hardware,
storage, OTA, voice, and board-identification details.

## Secrets

Copy [`include/secrets.example.h`](include/secrets.example.h) to
`include/secrets.local.h`. The local file is ignored by Git. Never commit Wi-Fi
credentials, Supabase keys, access tokens, or device-specific secrets.

Astrolabe uses the Castalia authentication and voice services when configured.
Personal rhythm, profile, and other local-first state must remain on the device
unless the user explicitly invokes an online feature.

## Product and workflow

- [`docs/development-plan.md`](docs/development-plan.md) — campaign product roadmap
- [`docs/marketing-strategy.md`](docs/marketing-strategy.md) — positioning and launch plan
- [`docs/BACKLOG.md`](docs/BACKLOG.md) — detailed work ledger
- [`docs/WORKFLOW.md`](docs/WORKFLOW.md) — issues, PRs, CI, and hardware QA
- [`docs/pocketwatch.md`](docs/pocketwatch.md) — product notes

Issue branches start from `integration` and PRs target `integration`. Firmware is
promoted to `main` only after the documented physical-device build and flash QA
gate.

## Product safety baseline

Astrolabe treats face, biometric, and sensor-derived signals as uncertain cues
for reflection. It must not infer identity, personality, truthfulness,
diagnosis, intent, or a stable mental state from those signals. The shared
wording lives in [`include/astrolabe_baseline.h`](include/astrolabe_baseline.h).
