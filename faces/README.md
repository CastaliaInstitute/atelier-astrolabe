# Mynah clock faces

**Canonical implementation today:** [`sketches/Astrolabe/faces/`](../sketches/Astrolabe/faces/)

This directory is the **repo-root home** for face modules as we migrate off monolithic `PocketMynah.ino`. Face code must use **`mynah::Display` / `mynah::Touch`** only (see [`include/mynah_hal/`](../include/mynah_hal/)), not board-specific drivers.

## Current faces (Astrolabe)

| Directory (Astrolabe) | Role |
|------------------------|------|
| `classic_analog/` | Hue analog / commonplace home |
| `apocalypso/` | Weather |
| `digital/` | Digital local time |
| `spotify/` | Now playing |
| `astrology/` | Transit wheel |
| `moon/` | Lunar phase |
| `calcifer/` | CalDAV countdown |
| `castalia/` | Sign-in QR |

## Planned aliases (future dirs here)

| Future `faces/` name | Notes |
|----------------------|--------|
| `hue_face/` | May alias classic analog hue ring |
| `astrolabe_face/` | Product shell chrome |
| `florilegium_face/` | Backlog |
| `hue_clock_face/` | Backlog |

## Adding a face

1. Implement under `sketches/Astrolabe/faces/<name>/` (or here after migration).
2. Extend `ClockFace` in `pm_faces.h`.
3. Wire `pm_faces_draw()` in `pm_clock.cpp`.
4. Validate in **Level 1** host viewer or hardware QA loop.

Simulation: [`docs/simulation.md`](../docs/simulation.md).
