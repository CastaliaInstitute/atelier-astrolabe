# Clock faces

Each face is a self-contained module under `faces/<name>/`. The app shell (`Astrolabe.ino`) handles WiFi, voice, and gestures; faces only draw UI and optional voice prompts.

## Layout

| Directory | Role |
|-----------|------|
| `shared/` | Rainbow rim, progress ring, voice waves, polar labels, HSV helpers |
| `classic_analog/` | Hue analog clock (commonplace journal home) |
| `apocalypso/` | Weather + impact |
| `digital/` | Large digital local time |
| `spotify/` | Vinyl Queue — browse stream, tap to commit (M1/M2) |
| `astrology/` | Transit wheel, zodiac glyphs, voice chart |
| `moon/` | Phase disk, texture, daily fortune |
| `calcifer/` | Hue Daywheel (rolling 12h hue ring + event wedges + CalDAV) |
| `focus/` | Pomodoro productivity timer with work, short break, and long break presets |
| `castalia/` | Sign-in QR (Settings hub page) |
| `settings/` | WiFi status + settings chrome |
| `synastry/` | Partner/family dual natal wheel + aspect highlights |
| `spectrum/` | Dual FFT bars: mic in (inner), speaker out (outer) |
| `chakra/` | Chakra symbols + solfeggio tones |
| `tibetan_bowl/` | Singing bowl; drag rainbow rim to strike |
| `rocket/` | Launch clock — upcoming launches on a 14-day dial |
| `radar/` | BLE peer radar — RSSI rings + gyro bearing |
| `biometrics/` | WiFi + BLE + IMU + audio wellness-signal inference |
| `faculty/` | ask-faculty recents + bust portrait |
| `weather/` | 24h radial temp + humidity rings, current conditions center |
| `quotes/` | Quote of the day + faculty bust |
| `ocarina/` | Touch-playable clay ocarina with key changes |
| `bongo/` | Touch-playable drum; center taps are low, rim taps are high |
| `pandrum/` | 14-note touch-playable handpan / pan drum |
| `piano/` | One-octave circular piano with white keys outside and black keys inside |
| `level/` | IMU rolling-sphere level; top of the display is forward |
| `alethiometer/` | Golden compass face: 36 Noto Emoji symbols, three question needles, one answer needle |
| `lenormand/` | Daily 36-card Lenormand face using the shared monochrome Noto Emoji glyphs |

## Public API per face

- `pm_face_<name>.h` — draw functions and face-specific voice helpers
- `pm_faces.h` / `pm_clock.cpp` — `ClockFace` enum, `pm_faces_draw()`, swipe cycle

## Adding a face

1. Create `faces/<name>/pm_face_<name>.{h,cpp}`.
2. Add a `ClockFace` value to `pm_faces.h` (before `kNumFaces`).
3. Wire `pm_face_<name>_draw()` in `pm_clock.cpp` (`pm_faces_draw` switch).
4. Set `banner_low` / rainbow flags in `pm_faces_draw()` if needed.

PlatformIO compiles all `*.cpp` under `sketches/Astrolabe/` recursively.
