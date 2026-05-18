# Clock faces

Each face is a self-contained module under `faces/<name>/`. The app shell (`Astrolabe.ino`) handles WiFi, voice, and gestures; faces only draw UI and optional voice prompts.

## Layout

| Directory | Role |
|-----------|------|
| `shared/` | Rainbow rim, progress ring, voice waves, polar labels, HSV helpers |
| `classic_analog/` | Hue analog clock (commonplace journal home) |
| `apocalypso/` | Weather + impact |
| `digital/` | Large digital local time |
| `spotify/` | Now playing + transport bar |
| `astrology/` | Transit wheel, zodiac glyphs, voice chart |
| `moon/` | Phase disk, texture, daily fortune |
| `calcifer/` | Hue Daywheel (rolling 12h hue ring + event wedges + CalDAV) |
| `castalia/` | Sign-in QR |
| `synastry/` | Partner/family dual natal wheel + aspect highlights |
| `spectrum/` | Dual FFT bars: mic in (inner), speaker out (outer) |
| `chakra/` | Chakra symbols + solfeggio tones (swipe, tap) |

## Public API per face

- `pm_face_<name>.h` — draw functions and face-specific voice helpers
- `pm_faces.h` / `pm_clock.cpp` — `ClockFace` enum, `pm_faces_draw()`, swipe cycle

## Adding a face

1. Create `faces/<name>/pm_face_<name>.{h,cpp}`.
2. Add a `ClockFace` value to `pm_faces.h` (before `kNumFaces`).
3. Wire `pm_face_<name>_draw()` in `pm_clock.cpp` (`pm_faces_draw` switch).
4. Set `banner_low` / rainbow flags in `pm_faces_draw()` if needed.

PlatformIO compiles all `*.cpp` under `sketches/Astrolabe/` recursively.
