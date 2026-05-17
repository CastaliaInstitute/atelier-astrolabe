# PocketMynah backlog

Track firmware features and todos for this repo. Product architecture and phased goals live in [`pocketwatch.md`](pocketwatch.md); build limits are in [`README.md`](../README.md#limits-mvp).

## Conventions

| Marker | Meaning |
|--------|---------|
| `[ ]` | Not started |
| `[~]` | In progress |
| `[x]` | Done — move to **Done** with date |

Optional priority prefix: **P0** (blocker / MVP), **P1** (next), **P2** (later).

Edit this file when you start or finish work. Keep **In progress** to 1–3 items. Link commits or PRs on **Done** lines when useful.

---

## In progress

- [~] **P1** Astrology / transits face: full-screen chart + planet/sign icons

---

## Features

### Voice UX (side buttons + visuals)

- [x] **P1** **STT on PWR, TTS on BOOT** — PWR hold → STT + inward wave UI; BOOT → replay last TTS (outward waves) or CalDAV agenda when no cache; Astrology BOOT = text reading, PWR hold = PCM; Moon PWR/BOOT split.

### Sky / astrology faces

- [ ] **P1** **Castalia ephemeris server** (dependency — **mynah / Supabase**, not firmware-only) — replace on-watch approximations in [`pm_transit.cpp`](../sketches/PocketMynah/pm_transit.cpp) (simplified Sun/Moon/planet math) with a Castalia Edge Function or service hosting **Swiss Ephemeris** (or equivalent licensed ephemeris). API: `epochSeconds`, lat/lon (optional), body list → tropical ecliptic longitudes (and lat/dist if needed). PocketMynah calls with Castalia JWT; cache responses briefly on device. Unblocks accurate **transits**, **celestial map**, and natal comparisons. Track implementation in mynah; astrolabe task: `pm_ephemeris_fetch` client + fallback to local `pm_transit` when offline.

- [ ] **P1** **Celestial map face** — new `ClockFace` (swipe cycle): full-screen **current sky** for observer time/place — plot **celestial bodies** (Sun, Moon, planets via `pm_transit` / extended ephemeris; optional bright stars later). Round polar layout (zenith center or horizon ring TBD); body icons (shared with transits face icon set). Requires NTP + valid time; optional geo/lat-lon from Wi‑Fi geo or NVS (see local-TZ task). Distinct from **Astrology / transits** (natal + zodiac wheel); reference Android [`MoonPhaseFace`](https://github.com/CastaliaInstitute/mynah/blob/main/android/app/src/main/java/institute/castalia/mynah/ui/MoonPhaseFace.kt) for moon presentation patterns only.

- [ ] **P1** **Natal chart face** — dedicated `ClockFace` (or mode on Astrology face): full **natal wheel** from birth data in NVS (`pm_birth_nvs`, serial `birth Y M D H MI`) — all major bodies + Asc/MC when ephemeris server supports houses; static chart for birth moment vs live **transits** overlay optional. Same round layout language as transits face (signs, houses, aspect lines TBD); planet/sign **icons** not abbreviations. Requires stored birth + accurate **Castalia ephemeris server**; distinct from transit-only view in `draw_astrology_face` (today: natal Sun marker only).

- [x] **P1** **Moon phase face** — `ClockFace::Moon` with `pm_transit` illumination disk; PWR hold STT + moon system prompt; BOOT spoken phase brief via `pm_voice_post_message`.

### Schedule / accessibility (Calcifer CalDAV)

- [x] **P1** **Autism countdown face** — `CalciferCountdown` face + `pm_calcifer`; 5‑minute “ending soon” visual cue; BOOT agenda when no replay cache.

### Phase 2 (round UI + voice parity)

- [ ] **P1** Faculty bust thumbnail on watch (`faculty-bust` HTTP) — see [pocketwatch.md § Backend](pocketwatch.md#backend-reuse)
- [ ] **P1** `ask-faculty` routing parity with Android voice flows (via `voice-pipeline` or direct)
- [ ] **P2** Round UI polish: safe-area inset, lower-arc touch targets — [pocketwatch.md § Experience](pocketwatch.md#experience-principles)
- [ ] **P2** Deep sleep / wake between interactions (battery honesty)

### Phase 3 (satellite link + updates)

- [ ] **P2** BLE or LAN presence with home Mynah — [pocketwatch.md § Phased delivery](pocketwatch.md#phased-delivery)
- [ ] **P2** OTA firmware updates (GitHub or custom bucket TBD)

### Ambient / delight (low priority)

- [ ] **P2** **Koi pond face** — optional `ClockFace`: animated pond (water ripple/refraction effects, drifting koi sprites); idle ambient mode when not interacting; keep CPU/GPU budget modest on ESP32-S3 + CO5300 canvas

### Alethiometer face

- [ ] **P2** **Alethiometer face** — new `ClockFace` styled like a golden compass: **36 symbols** around the dial, **4 hands** (3 short + 1 long). Flow: user **STT** a question (PWR / voice path) → send transcript to **LLM** (Castalia `voice-pipeline` or dedicated edge function) → model returns which **three symbols** the short hands point at (phrasing the question) + which symbol the **long hand** indicates (the answer) → animate hands to those positions on the round display → **TTS** the textual interpretation. Needs symbol index/art in flash, hand-angle math, JSON contract for `{ question, needles[3], answer_needle, interpretation }`, and Castalia auth. Not a real divination backend — LLM-driven narrative UX; document as experimental/delight.

---

## Tasks

Derived from [README limits](../README.md#limits-mvp) and [open questions](pocketwatch.md#open-questions).

- [x] **P0** Setup debugging to console — documented in README (`pio device monitor`, 115200, `CORE_DEBUG_LEVEL`, exception decoder)
- [x] **P0** Setup JTAG over USB — documented in README (303A:1001, `pio debug`)

- [ ] **P1** TLS: replace `WiFiClientSecure::setInsecure()` with CA pinning / bundle
- [x] **P1** Voice: handle `voice-pipeline` responses without `audioBase64` (plain `ask-faculty`-only)
- [ ] **P1** Auth: document anon-only vs signed-in behavior in README once Castalia flow is stable
- [x] **P1** Classic analog clock face: center dial on round display and use full-screen safe area (466×466); fix layout/offset in `PocketMynah.ino` analog draw path
- [ ] **P1** Astrology / transits face (`draw_astrology_face`, `pm_transit`): full-screen chart (maximize wheel on 466×466, trim or relocate title/date/footer chrome); replace text labels (`ARI`…`PIS`, `pm_ephem_body_label` abbreviations) with bitmap icons for zodiac signs and planets (GFX sprites or minimal glyph set in flash)
- [x] **P1** Remove gesture debug labels (e.g. swipe up/down banners on clock face); drop or gate `g_gesture_banner` / `pm_gesture` debug UI for production
- [ ] **P2** Clock faces: local timezone (NTP + geo or user setting); README currently notes UTC-only
- [x] **P2** Voice: raise or stream around HTTP body/response caps in `pm_voice.cpp` for long TTS
- [ ] **P2** Confirm TTS output path (codec, amp, speaker) for pinned Waveshare SKU in README
- [ ] **P2** Wake word — explicitly deferred; **PTT only** for v1
- [x] **P1** Port `pm_calcifer` HTTP client from mynah pocketwatch (`calcifer-status` JSON parse, auth headers via `pm_castalia_auth_apply_headers`)
- [x] **P2** Port `pm_voice_post_calcifer_clock_brief` (`voice-pipeline` `face=clock_agenda`) from mynah — spoken agenda complement to countdown face

---

## Done

- [x] **2026-05-17** Charging ripples on rainbow rim when USB-C — [PR #7](https://github.com/CastaliaInstitute/astrolabe/pull/7)
- [x] **2026-05-16** Voice UX: PWR STT + inward waves; BOOT TTS replay + outward waves; last-reply cache
- [x] **2026-05-16** Moon phase clock face; README serial/JTAG bring-up notes
- [x] **2026-05-16** Castalia QR sign-in + NVS session JWT; voice/Spotify use `pm_castalia_auth_apply_headers`
- [x] **2026-05-16** Voice: 1.5 MiB response cap, JSON completeness check, TTS playback drain/abort, text-only pipeline replies
- [x] **2026-05-16** `pm_calcifer` + **CalciferCountdown** clock face; BOOT spoken agenda via `pm_voice_begin_clock_agenda`
- [x] **2026-05-16** Gesture banners gated (`MYNAH_DEBUG_GESTURES`); analog dial centered on 466×466
- [x] **2026-05-15** Phase 0: PlatformIO toolchain, `secrets.example.h`, PocketMynah flashes on ESP32-S3 1.75C class board
- [x] **2026-05-15** Phase 1: Wi‑Fi + NTP; `voice-pipeline` text (`message`) and PCM (`audioBase64` in); on-device MP3 via minimp3 + ES8311
- [x] **2026-05-15** Hue clock faces: analog, Apocalypso, digital local; swipe gestures (`pm_gesture`)
- [x] **2026-05-15** PTT hold-to-talk mic capture → `voice-pipeline` STT path
- [x] **2026-05-15** Astrology face scaffolding: birth NVS + serial `birth` command; PWR/BOOT tap hooks
- [x] **2026-05-15** Repo split from mynah `pocketwatch/` → astrolabe; design doc at `docs/pocketwatch.md`
