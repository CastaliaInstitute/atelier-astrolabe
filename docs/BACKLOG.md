# Astrolabe backlog

Track firmware features and todos for this repo. Product architecture and phased goals live in [`pocketwatch.md`](pocketwatch.md); build limits are in [`README.md`](../README.md#limits-mvp).

## Conventions

| Marker | Meaning |
|--------|---------|
| `[ ]` | Not started |
| `[~]` | In progress |
| `[x]` | Done — move to **Done** with date |

Optional priority prefix: **P0** (blocker / MVP), **P1** (next), **P2** (later).

Edit this file when you start or finish work. Keep **In progress** to 1–3 items.

**GitHub + branches:** Each active item should have a **GitHub issue** and branch `feature/<#>-slug` or `fix/<#>-slug`. See [`WORKFLOW.md`](WORKFLOW.md) (Cursor Cloud agents start from the issue URL). In **In progress**, note `Issue: #N`.

**Git:** **Commit between feature implementations** — one focused commit per completed backlog item (firmware + backlog update), then start the next feature. Merge via **PR** (`Closes #N`). Avoid stacking unrelated features in one branch or commit.

**Hardware QA (agents):** After a **new or changed clock face**, flash the watch, show that face, **screenshot**, and **evaluate** before marking done. See [`.cursor/rules/hardware-qa.mdc`](../.cursor/rules/hardware-qa.mdc) and [`astrolabe-esp-mcp.mdc`](../.cursor/rules/astrolabe-esp-mcp.mdc).

**Git:** Issue branches merge via **PR → `integration`**; promote to **`main`** with `./scripts/promote-integration.sh --flash-ok` after build + flash QA. See [`WORKFLOW.md`](WORKFLOW.md).

---

## In progress

_(none — issue #11 ready for integration review 2026-05-17)_

---

## Features

### Commonplace (Directus journal) — **P0 priority**

- [x] **P0** **Commonplace journal from home face** — **ClassicAnalog** hue home, **PWR hold** → `pm_commonplace` → `mynah-pocket-journal`; banners `saved: …` / `journal: …`. Server deploy + on-device verify still operator tasks (`supabase functions deploy mynah-pocket-journal`, Castalia sign-in).

### Voice UX (side buttons + visuals)

- [x] **P1** **STT on PWR, TTS on BOOT** — PWR hold → STT + inward wave UI; BOOT → replay last TTS (outward waves) or CalDAV agenda when no cache; Astrology BOOT = text reading, PWR hold = PCM; Moon PWR/BOOT split.

### Sky / astrology faces

- [ ] **P1** Astrology / transits face: full-screen chart + planet/sign icons — Issue [#3](https://github.com/CastaliaInstitute/astrolabe/issues/3); glyphs done; polish chrome / aspects TBD

- [ ] **P1** **Castalia ephemeris server** (dependency — **mynah / Supabase**, not firmware-only) — host at [**ephemeris.castalia.institute**](https://ephemeris.castalia.institute): Swiss Ephemeris (or equivalent) Edge Function/API. Replace on-watch approximations in [`pm_transit.cpp`](../sketches/Astrolabe/pm_transit.cpp). API: birth datetime + lat/lon → natal longitudes, houses, synastry aspects between two charts. Astrolabe calls with Castalia JWT; cache briefly on device. Unblocks **transits**, **celestial map**, **natal**, **synastry**. Firmware: `pm_ephemeris_fetch` + fallback to local `pm_transit` when offline.

- [ ] **P1** **Orrery face** — new `ClockFace`: **orrery** view with the **Sun** at center and **planets on concentric rings** (orbital radii scaled for round display; positions from `pm_transit` or **Castalia ephemeris server** when available). Optional: animate slow orbital motion over time; tap a planet for label. Distinct from flat **celestial map** (sky dome) and **transits** zodiac wheel.

- [ ] **P1** **Celestial map face** — new `ClockFace` (swipe cycle): full-screen **current sky** for observer time/place — plot **celestial bodies** (Sun, Moon, planets via `pm_transit` / extended ephemeris; optional bright stars later). Round polar layout (zenith center or horizon ring TBD); body icons (shared with transits face icon set). Requires NTP + valid time; optional geo/lat-lon from Wi‑Fi geo or NVS (see local-TZ task). Distinct from **Astrology / transits** (natal + zodiac wheel); reference Android [`MoonPhaseFace`](https://github.com/CastaliaInstitute/mynah/blob/main/android/app/src/main/java/institute/castalia/mynah/ui/MoonPhaseFace.kt) for moon presentation patterns only.

- [ ] **P1** **Natal chart face** — dedicated `ClockFace` (or mode on Astrology face): full **natal wheel** from birth data in NVS (`pm_birth_nvs`, serial `birth Y M D H MI`) — all major bodies + Asc/MC when ephemeris server supports houses; static chart for birth moment vs live **transits** overlay optional. Same round layout language as transits face (signs, houses, aspect lines TBD); planet/sign **icons** not abbreviations. Requires stored birth + accurate **Castalia ephemeris server**; distinct from transit-only view in `draw_astrology_face` (today: natal Sun marker only).

- [ ] **P1** **Synastry face** — new `ClockFace`: **synastry** (and related) charts for **partners, children, family** via **ephemeris.castalia.institute** (natal pairs → aspect grid or dual-wheel overlay on round display). **Swipe up/down** cycles chart targets (e.g. user↔partner, user↔child, child↔child composites TBD). **STT** (PWR) to ask questions about the active chart; **TTS** / text reading of highlights (BOOT or auto-brief). Store named profiles in NVS (`pm_chart_profiles`): birth date, place → geocode lat/lon, optional birth time (default noon local if unknown). **Demo seed profiles:** **Camille** (1984-09-23, Exeter, NH) partner; **Aidan** (2003-09-12, Littleton, CO); **Finn** (2024-04-30, Monument, CO); **Aleia** (2025-05-04, Monument, CO). Depends on ephemeris server + user natal in `pm_birth_nvs`.

- [x] **P1** **Moon phase face** — `ClockFace::Moon` with `pm_transit` illumination disk; PWR hold STT + moon system prompt; BOOT spoken phase brief via `pm_voice_post_message`.

### Schedule / accessibility (Calcifer CalDAV)

- [x] **P1** **Autism countdown face** — `CalciferCountdown` face + `pm_calcifer`; 5‑minute “ending soon” visual cue; BOOT agenda when no replay cache.

### Glance / utility faces

- [ ] **P1** **Weather face** — new `ClockFace`: current conditions + short forecast for observer location. **Location:** Wi‑Fi geo / `pm_geo_tz` or NVS lat-lon from mDNS config. **Data:** Castalia Edge Function (API keys server-side, same pattern as `calcifer-status`) — temp, icon/condition, hi/lo, optional hourly strip on round display. Poll on interval when face visible + Wi‑Fi; cache last good response offline. Optional: STT “what’s the weather?” on this face; tie icon art to ambient hue. Depends on Castalia JWT + NTP.

- [ ] **P1** **Rotating Earth face** — new `ClockFace`: **slowly rotating globe** on round display with **day/night terminator** from UTC + optional observer lon (NTP; `pm_geo_tz` or NVS). **Night side** dim, **day side** lit; optional dot for user location. **Weather:** overlay conditions on the map or a compact HUD (reuse **Weather face** / Castalia weather API — clouds/precip bands, temp at pin). Animate rotation tied to time (sidereal or simple spin). Distinct from flat **weather** summary and **orrery** (heliocentric); keep draw cost bounded on CO5300 canvas.

### Phase 2 (round UI + voice parity)

- [ ] **P1** **Faculty face** — new `ClockFace` for **ask-faculty** conversations. **Flow:** **STT** question (PWR) → **router step** (LLM or edge fn) infers **which faculty** from utterance (“ask Einstein…”, “what would Curie say…”) → **`ask-faculty`** with resolved `facultySlug` + **conversation history** in prompt → **TTS** reply. **Bust:** `GET` [`faculty-bust`](https://github.com/CastaliaInstitute/mynah/blob/main/supabase/functions/faculty-bust/index.ts), download/cache portrait in flash or PSRAM (`pm_faculty_bust`), show on face during chat. **Swipe up/down:** cycle **recent faculty** (NVS list of slugs last spoken with); continue same thread per faculty. **Commonplace:** log each turn via [`commonplaceDirectus`](https://github.com/CastaliaInstitute/mynah/blob/main/supabase/functions/_shared/commonplaceDirectus.ts) (`kind: conversation`, `route: ask-faculty`, `facultySlug`); fetch recent entries for that faculty+user to build **history** for prompts (server-side or pocket pulls summary). Parity with Android [`GlowScreen`](https://github.com/CastaliaInstitute/mynah/blob/main/android/app/src/main/java/institute/castalia/mynah/ui/GlowScreen.kt) voice route. See [pocketwatch.md § Backend](pocketwatch.md#backend-reuse).
- [ ] **P2** Round UI polish: safe-area inset, lower-arc touch targets — [pocketwatch.md § Experience](pocketwatch.md#experience-principles)
- [ ] **P1** **Very low power mode (battery)** — when **not USB-C charging** (AXP2101 / PMU: on battery only), enter aggressive low-power after idle timeout: dim or **blank AMOLED**, stop nonessential polling (Spotify, CalDAV, weather, etc.), CPU **deep sleep** / light sleep between ticks. **Wake on button press** — **PWR** (AXP IRQ) and **BOOT** (GPIO) restore full UI + Wi‑Fi reconnect as needed. While **charging**, stay in normal ambient mode (optional charging ripples on rim). Tune idle timeout and RTC/NVS retention. See [pocketwatch.md § Experience](pocketwatch.md#experience-principles) battery honesty.

### Phase 3 (satellite link + updates)

- [ ] **P2** BLE or LAN presence with home Mynah — [pocketwatch.md § Phased delivery](pocketwatch.md#phased-delivery)
- [ ] **P2** OTA firmware updates (GitHub or custom bucket TBD)

### Ambient / delight (low priority)

- [ ] **P2** **Koi pond face** — optional `ClockFace`: animated pond (water ripple/refraction effects, drifting koi sprites); idle ambient mode when not interacting; keep CPU/GPU budget modest on ESP32-S3 + CO5300 canvas

- [ ] **P2** **Tibetan bowl face** — new `ClockFace`: **touch the screen circumference** (polar hit-test on outer ring) to strike/play a singing-bowl tone; map touch angle → phase/pan and strike intensity → amplitude/decay. Synthesize fundamentals + harmonics via I2S (`pm_speaker` / ES8311) or precomputed samples in flash. **Swipe up/down** cycles **bowl presets** (different base frequency, decay, overtone mix). Visual: bowl graphic + ripple on strike. Respect existing gesture face-swipe zones so bowl face does not fight global navigation.

- [ ] **P2** **Sound mandala face** — new `ClockFace`: **audio-reactive** “trippy” visualization (radial mandala / kaleidoscope on round 466×466 canvas) **synchronized to live input** from onboard mic (`pm_mic`) and/or ambient room audio. Map band energy or envelope → hue, rotation, petal count, pulse radius; smooth decay when quiet. Optional: react to TTS playback line-out if tap available. Keep frame rate and FFT cost bounded on ESP32-S3; no voice/STT on this face unless user swipes away.

### Alethiometer face

- [ ] **P2** **Alethiometer face** — new `ClockFace` styled like a golden compass: **36 symbols** around the dial, **4 hands** (3 short + 1 long). Flow: user **STT** a question (PWR / voice path) → send transcript to **LLM** (Castalia `voice-pipeline` or dedicated edge function) → model returns which **three symbols** the short hands point at (phrasing the question) + which symbol the **long hand** indicates (the answer) → animate hands to those positions on the round display → **TTS** the textual interpretation. Needs symbol index/art in flash, hand-angle math, JSON contract for `{ question, needles[3], answer_needle, interpretation }`, and Castalia auth. Not a real divination backend — LLM-driven narrative UX; document as experimental/delight.

- [ ] **P2** **I Ching face** — new `ClockFace`: cast **hexagram** (e.g. three-coin or simplified RNG + optional shake/tap ritual on round display); render **six lines** (yin/yang, changing lines) and hexagram number/name. **STT** question (PWR) → LLM + hexagram context (primary, optional relating hexagram) → **TTS** interpretation. Static lookup table for 64 hexagram names in flash; edge function or `voice-pipeline` `face=iching` JSON contract. Experimental/delight; distinct from Alethiometer (symbols/hands vs lines/hexagrams).

---

## Tasks

Derived from [README limits](../README.md#limits-mvp) and [open questions](pocketwatch.md#open-questions).

- [x] **P0** Setup debugging to console — documented in README (`pio device monitor`, 115200, `CORE_DEBUG_LEVEL`, exception decoder)
- [x] **P0** Setup JTAG over USB — documented in README (303A:1001, `pio debug`)

- [ ] **P1** **mDNS + LAN config page** — advertise **`astrolabe-xxxx.local`** on the LAN (`ESPmDNS`, `xxxx` = short id from MAC/chip id). While on Wi‑Fi, run a lightweight **HTTP config UI** (ESPAsyncWebServer or `WebServer`) at that hostname: edit **NVS-backed** settings without serial — Wi‑Fi creds (if not compile-time only), user **birth** (`pm_birth_nvs`), **synastry profiles**, default face, optional Supabase URL override for dev. POST saves → NVS commit → confirm in UI. Document URL in README; require LAN or simple setup-token if adding auth later.

- [ ] **P1** TLS: replace `WiFiClientSecure::setInsecure()` with CA pinning / bundle
- [x] **P1** Voice: handle `voice-pipeline` responses without `audioBase64` (plain `ask-faculty`-only)
- [ ] **P1** Auth: document anon-only vs signed-in behavior in README once Castalia flow is stable — Issue [#5](https://github.com/CastaliaInstitute/astrolabe/issues/5)
- [x] **P1** Classic analog clock face: center dial on round display and use full-screen safe area (466×466); fix layout/offset in `Astrolabe.ino` analog draw path
- [x] **P1** **Home face = Hue only** — `MYNAH_HUE_HOME_ONLY` (default 1): ClassicAnalog = ambient hue + 24h rainbow only; clock hands on DigitalLocal / Apocalypso.
- [ ] **P1** **Charging ripples on rainbow rim** — Issue [#4](https://github.com/CastaliaInstitute/astrolabe/issues/4) — when **USB-C charging** detected (AXP2101 / PMU: `VBUS` or charge-status register via I2C, same bus as PWR key), animate **gentle ripples** along the **bottom arc** of the **24h rainbow ring** (`draw_circumference_rainbow_24h`); subtle amplitude, slow phase — ambient “filling” cue without bright alerts. Off when on battery only; works on home/Hue face and any face that shows the rim.
- [x] **P1** Astrology chart glyphs — zodiac + planet alpha masks (`embed_*_glyphs.py`, `pm_zodiac_glyphs`); wheel radius `R−10`. Remaining: trim footer chrome, aspect lines, ephemeris server accuracy.
- [x] **P1** Remove gesture debug labels (e.g. swipe up/down banners on clock face); drop or gate `g_gesture_banner` / `pm_gesture` debug UI for production
- [x] **P1** Moon face UX bugfixes — Closes [#1](https://github.com/CastaliaInstitute/astrolabe/issues/1): swipe up/down cycles faces; removed phase % label and footer hints
- [ ] **P1** **Bugfix: circadian hue mapping** — Issue [#2](https://github.com/CastaliaInstitute/astrolabe/issues/2) — replace linear `sec_of_day * (360/86400)` (midnight reads red/wrong) with **keyframed hue** + interpolation in one helper used by home face fill and `pm_face_draw_circumference_rainbow_24h` ([`pm_clock.cpp`](../sketches/Astrolabe/faces/pm_clock.cpp), [`pm_face_draw.cpp`](../sketches/Astrolabe/faces/shared/pm_face_draw.cpp)). **Anchor:** midnight **250°** indigo → pre-dawn magenta → sunrise **340°** rose → morning amber **40°** → noon **120°** green → afternoon cyan **185°** → dusk **245°** → night violet **270°** → wrap to 250°. **Stops (hour, hue°):** `(0,250) (3,270) (5,310) (6,340) (8,40) (10,70) (12,120) (15,185) (17,215) (19,245) (21,270) (24,250)`. **Display:** restrained face — low-V dark bg from hue (~45% S, 8% V), accents higher S/V; optional debug band names: Nocturne 00–04, Aurora 05–07, Solar 08–11, Meridian 12–14, Zephyr 15–17, Vesper 18–20, Oracle 21–23. QA: screenshot home face at 00:00, 06:00, 12:00, 18:00.
- [ ] **P2** Clock faces: local timezone (NTP + geo or user setting); README currently notes UTC-only
- [x] **P2** Voice: raise or stream around HTTP body/response caps in `pm_voice.cpp` for long TTS
- [ ] **P2** Confirm TTS output path (codec, amp, speaker) for pinned Waveshare SKU in README
- [ ] **P2** Wake word — explicitly deferred; **PTT only** for v1
- [x] **P1** Port `pm_calcifer` HTTP client from mynah pocketwatch (`calcifer-status` JSON parse, auth headers via `pm_castalia_auth_apply_headers`)
- [x] **P2** Port `pm_voice_post_calcifer_clock_brief` (`voice-pipeline` `face=clock_agenda`) from mynah — spoken agenda complement to countdown face

---

## Done

- [x] **2026-05-17** Menstrual cycle face: NVS-only circular cycle ring, fertile/ovulation bands, tap day-1 logging, and swipe length presets. PR [#12](https://github.com/CastaliaInstitute/astrolabe/pull/12); Closes [#11](https://github.com/CastaliaInstitute/astrolabe/issues/11)
- [x] **2026-05-17** Astrology face polish: ephemeris fetch + glyph wheel. Closes [#3](https://github.com/CastaliaInstitute/astrolabe/issues/3)
- [x] **2026-05-17** Charging ripples on rainbow rim when USB-C. Closes [#4](https://github.com/CastaliaInstitute/astrolabe/issues/4)
- [x] **2026-05-17** Circadian 24h hue keyframes (`pm_circadian_hue`). Closes [#2](https://github.com/CastaliaInstitute/astrolabe/issues/2)
- [x] **2026-05-17** Auth docs: README explains anon bearer vs Castalia JWT for voice, commonplace, and Calcifer. Closes [#5](https://github.com/CastaliaInstitute/astrolabe/issues/5)
- [x] **2026-05-17** Moon face UX: swipe cycles faces; removed phase label and footer hints. Closes [#1](https://github.com/CastaliaInstitute/astrolabe/issues/1)
- [x] **2026-05-16** Commonplace journal from hue home (PWR hold → `mynah-pocket-journal` via `pm_commonplace`)
- [x] **2026-05-16** Hue-only home face (`MYNAH_HUE_HOME_ONLY`); astrology planet + zodiac glyphs on chart
- [x] **2026-05-16** Voice UX: PWR STT + inward waves; BOOT TTS replay + outward waves; last-reply cache
- [x] **2026-05-16** Moon phase clock face; README serial/JTAG bring-up notes
- [x] **2026-05-16** Castalia QR sign-in + NVS session JWT; voice/Spotify use `pm_castalia_auth_apply_headers`
- [x] **2026-05-16** Voice: 1.5 MiB response cap, JSON completeness check, TTS playback drain/abort, text-only pipeline replies
- [x] **2026-05-16** `pm_calcifer` + **CalciferCountdown** clock face; BOOT spoken agenda via `pm_voice_begin_clock_agenda`
- [x] **2026-05-16** Gesture banners gated (`MYNAH_DEBUG_GESTURES`); analog dial centered on 466×466
- [x] **2026-05-15** Phase 0: PlatformIO toolchain, `secrets.example.h`, Astrolabe flashes on ESP32-S3 1.75C class board
- [x] **2026-05-15** Phase 1: Wi‑Fi + NTP; `voice-pipeline` text (`message`) and PCM (`audioBase64` in); on-device MP3 via minimp3 + ES8311
- [x] **2026-05-15** Hue clock faces: analog, Apocalypso, digital local; swipe gestures (`pm_gesture`)
- [x] **2026-05-15** PTT hold-to-talk mic capture → `voice-pipeline` STT path
- [x] **2026-05-15** Astrology face scaffolding: birth NVS + serial `birth` command; PWR/BOOT tap hooks
- [x] **2026-05-15** Repo split from mynah `pocketwatch/` → astrolabe; design doc at `docs/pocketwatch.md`
