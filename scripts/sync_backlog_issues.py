#!/usr/bin/env python3
"""Create GitHub issues for BACKLOG.md items and print number→slug mapping."""
from __future__ import annotations

import json
import subprocess
import sys
import time
from dataclasses import dataclass

REPO = "CastaliaInstitute/astrolabe"


@dataclass
class BacklogIssue:
    key: str  # stable id for BACKLOG.md updates
    title: str
    labels: list[str]
    priority: str
    body: str
    state: str  # open | closed
    existing: int | None = None  # skip create if set


ITEMS: list[BacklogIssue] = [
    # --- existing ---
    BacklogIssue("moon-ux", "[Bug] Moon face: swipe vs tap, remove labels", ["bug"], "P1", "Backlog: Moon face UX bugfixes.", "closed", 1),
    BacklogIssue("circadian-hue", "[Bug] Circadian hue mapping (24h keyframes)", ["bug"], "P1", "Backlog: Bugfix circadian hue mapping.", "closed", 2),
    BacklogIssue("astrology-polish", "[Feature] Astrology face polish", ["enhancement"], "P1", "Backlog: Astrology / transits face polish.", "closed", 3),
    BacklogIssue("charging-ripples", "[Task] Charging ripples on rainbow rim when USB-C", ["enhancement"], "P1", "Backlog: Charging ripples on rainbow rim.", "closed", 4),
    BacklogIssue("auth-docs", "[Task] Document Castalia auth (anon vs signed-in) in README", ["enhancement"], "P1", "Backlog: Auth documentation.", "closed", 5),
    BacklogIssue("menstrual-cycle", "[Feature] Menstrual cycle face (circular, low text)", ["enhancement"], "P2", "Backlog: Menstrual cycle face.", "open", 11),
    # --- done, no issue yet ---
    BacklogIssue(
        "commonplace-journal",
        "[Feature] Commonplace journal from hue home face",
        ["enhancement"],
        "P0",
        """## Summary
ClassicAnalog hue home: **PWR hold** → `pm_commonplace` → `mynah-pocket-journal`; banners `saved: …` / `journal: …`.

## Status
Implemented in firmware (2026-05-16). Operator follow-ups: deploy `mynah-pocket-journal`, on-device Castalia sign-in verify.

## Backlog
`docs/BACKLOG.md` — Commonplace (Directus journal)

## Acceptance criteria
- [x] PWR hold routes to commonplace on home face
- [ ] Server deploy + on-device verify (operator)
""",
        "closed",
    ),
    BacklogIssue(
        "voice-stt-tts",
        "[Feature] STT on PWR, TTS on BOOT (voice UX)",
        ["enhancement"],
        "P1",
        """## Summary
PWR hold → STT + inward wave UI; BOOT → replay last TTS or CalDAV agenda; Astrology/Moon BOOT/PWR split.

## Status
Done (2026-05-16). See `docs/BACKLOG.md` Done section.

## Acceptance criteria
- [x] Builds and on-device voice paths work
""",
        "closed",
    ),
    BacklogIssue(
        "moon-phase-face",
        "[Feature] Moon phase clock face",
        ["enhancement"],
        "P1",
        """## Summary
`ClockFace::Moon` with `pm_transit` illumination disk; PWR STT + moon prompt; BOOT spoken phase brief.

## Status
Done (2026-05-16). UX polish tracked separately in #1.

## Acceptance criteria
- [x] Moon face in swipe cycle with voice hooks
""",
        "closed",
    ),
    BacklogIssue(
        "calcifer-countdown",
        "[Feature] Autism countdown (Calcifer) face",
        ["enhancement"],
        "P1",
        """## Summary
`CalciferCountdown` face + `pm_calcifer`; 5-minute ending-soon cue; BOOT agenda when no replay cache.

## Status
Done (2026-05-16).

## Acceptance criteria
- [x] Countdown face + CalDAV integration
""",
        "closed",
    ),
    BacklogIssue(
        "hue-home-only",
        "[Task] Home face = hue only (MYNAH_HUE_HOME_ONLY)",
        ["enhancement"],
        "P1",
        """## Summary
ClassicAnalog = ambient hue + 24h rainbow only; clock hands on DigitalLocal / Apocalypso.

## Status
Done (2026-05-16).

## Acceptance criteria
- [x] `MYNAH_HUE_HOME_ONLY` default on
""",
        "closed",
    ),
    BacklogIssue(
        "astrology-glyphs",
        "[Task] Astrology chart glyphs (zodiac + planet masks)",
        ["enhancement"],
        "P1",
        """## Summary
Zodiac + planet alpha masks on astrology wheel. Remaining polish: footer chrome, aspect lines, ephemeris accuracy (#3).

## Status
Glyphs landed (2026-05-16).

## Acceptance criteria
- [x] Glyphs render on wheel at R−10
""",
        "closed",
    ),
    BacklogIssue(
        "gesture-debug-off",
        "[Task] Remove gesture debug labels for production",
        ["enhancement"],
        "P1",
        """## Summary
Gate or remove `g_gesture_banner` / gesture debug UI (`MYNAH_DEBUG_GESTURES`).

## Status
Done (2026-05-16).

## Acceptance criteria
- [x] Production builds without swipe debug banners
""",
        "closed",
    ),
    BacklogIssue(
        "classic-analog-layout",
        "[Task] Classic analog dial centered on 466×466",
        ["enhancement"],
        "P1",
        """## Summary
Center analog dial on round display; full-screen safe area.

## Status
Done (2026-05-16).

## Acceptance criteria
- [x] Layout correct on CO5300 canvas
""",
        "closed",
    ),
    BacklogIssue(
        "voice-no-audio-base64",
        "[Task] Voice pipeline text-only replies (no audioBase64)",
        ["enhancement"],
        "P1",
        """## Summary
Handle `voice-pipeline` responses without `audioBase64` (plain ask-faculty text).

## Status
Done (2026-05-16).

## Acceptance criteria
- [x] Text-only responses do not crash playback path
""",
        "closed",
    ),
    BacklogIssue(
        "voice-http-caps",
        "[Task] Raise/stream voice HTTP body caps for long TTS",
        ["enhancement"],
        "P2",
        """## Summary
Raise or stream around HTTP body/response caps in `pm_voice.cpp` for long TTS.

## Status
Done (2026-05-16).

## Acceptance criteria
- [x] Large TTS payloads handled
""",
        "closed",
    ),
    BacklogIssue(
        "pm-calcifer-port",
        "[Task] Port pm_calcifer HTTP client from mynah",
        ["enhancement"],
        "P1",
        """## Summary
Port `pm_calcifer` from mynah pocketwatch (`calcifer-status` JSON, Castalia auth headers).

## Status
Done (2026-05-16).

## Acceptance criteria
- [x] Calcifer status fetch on device
""",
        "closed",
    ),
    BacklogIssue(
        "calcifer-clock-brief",
        "[Task] Port pm_voice_post_calcifer_clock_brief",
        ["enhancement"],
        "P2",
        """## Summary
Spoken agenda via `voice-pipeline` `face=clock_agenda` from mynah.

## Status
Done (2026-05-16).

## Acceptance criteria
- [x] BOOT agenda TTS on countdown/analog paths
""",
        "closed",
    ),
    BacklogIssue(
        "readme-serial-jtag",
        "[Task] README: serial monitor + JTAG bring-up",
        ["enhancement"],
        "P0",
        """## Summary
Document `pio device monitor`, exception decoder, and JTAG (303A:1001, `pio debug`).

## Status
Done (2026-05-16).

## Acceptance criteria
- [x] README sections present
""",
        "closed",
    ),
    # --- open work ---
    BacklogIssue(
        "ephemeris-server",
        "[Feature] Castalia ephemeris server (ephemeris.castalia.institute)",
        ["enhancement"],
        "P1",
        """## Summary
Host Swiss Ephemeris (or equivalent) Edge Function/API at **ephemeris.castalia.institute**. Birth datetime + lat/lon → natal longitudes, houses, synastry aspects. Astrolabe uses Castalia JWT; cache on device. Unblocks transits, celestial map, natal, synastry.

## Backlog
`docs/BACKLOG.md` — Castalia ephemeris server

## Acceptance criteria
- [ ] API deployed with auth
- [ ] Firmware `pm_ephemeris_fetch` + offline fallback to `pm_transit`
- [ ] `docs/BACKLOG.md` updated
""",
        "open",
    ),
    BacklogIssue(
        "orrery-face",
        "[Feature] Orrery clock face",
        ["enhancement"],
        "P1",
        """## Summary
New `ClockFace`: Sun at center, planets on concentric rings (from `pm_transit` or ephemeris server). Optional slow animation; tap planet for label.

## Backlog
`docs/BACKLOG.md` — Orrery face

## Acceptance criteria
- [ ] Builds; face in swipe cycle
- [ ] Hardware QA screenshot
- [ ] `docs/BACKLOG.md` updated
""",
        "open",
    ),
    BacklogIssue(
        "celestial-map-face",
        "[Feature] Celestial map clock face",
        ["enhancement"],
        "P1",
        """## Summary
Full-screen current sky for observer time/place; round polar layout; body icons shared with transits face. NTP + geo/NVS lat-lon.

## Backlog
`docs/BACKLOG.md` — Celestial map face

## Acceptance criteria
- [ ] Builds; face in swipe cycle
- [ ] Hardware QA screenshot
- [ ] `docs/BACKLOG.md` updated
""",
        "open",
    ),
    BacklogIssue(
        "natal-chart-face",
        "[Feature] Natal chart clock face",
        ["enhancement"],
        "P1",
        """## Summary
Natal wheel from `pm_birth_nvs` / serial `birth` command; major bodies + Asc/MC when ephemeris server supports houses.

## Backlog
`docs/BACKLOG.md` — Natal chart face

## Acceptance criteria
- [ ] Builds; uses stored birth + ephemeris API
- [ ] Hardware QA screenshot
- [ ] `docs/BACKLOG.md` updated
""",
        "open",
    ),
    BacklogIssue(
        "synastry-face",
        "[Feature] Synastry clock face",
        ["enhancement"],
        "P1",
        """## Summary
Synastry charts for partners/family via ephemeris server; swipe cycles targets; STT/TTS; NVS profiles (`pm_chart_profiles`) with demo seed profiles per BACKLOG.

## Backlog
`docs/BACKLOG.md` — Synastry face

## Acceptance criteria
- [ ] Profile NVS + UI
- [ ] Hardware QA screenshot
- [ ] `docs/BACKLOG.md` updated
""",
        "open",
    ),
    BacklogIssue(
        "weather-face",
        "[Feature] Weather clock face",
        ["enhancement"],
        "P1",
        """## Summary
Current conditions + short forecast via Castalia Edge Function (like `calcifer-status`); Wi‑Fi geo or NVS lat-lon; poll when face visible.

## Backlog
`docs/BACKLOG.md` — Weather face

## Acceptance criteria
- [ ] Edge function + firmware face
- [ ] Hardware QA screenshot
- [ ] `docs/BACKLOG.md` updated
""",
        "open",
    ),
    BacklogIssue(
        "rotating-earth-face",
        "[Feature] Rotating Earth clock face",
        ["enhancement"],
        "P1",
        """## Summary
Rotating globe with day/night terminator; optional weather HUD overlay. Bounded draw cost on CO5300.

## Backlog
`docs/BACKLOG.md` — Rotating Earth face

## Acceptance criteria
- [ ] Face in swipe cycle
- [ ] Hardware QA screenshot
""",
        "open",
    ),
    BacklogIssue(
        "faculty-face",
        "[Feature] Faculty face — STT, ask-faculty, bust + TTS",
        ["enhancement"],
        "P1",
        """## Summary
New `ClockFace::Faculty`: STT → `ask-faculty` (history on Castalia). Watch stores recent faculty slugs + bust cache only; swipe up/down scrolls speakers, not transcripts.

## Backlog
`docs/BACKLOG.md` — Faculty face

## Acceptance criteria
- [ ] PTT → ask-faculty → TTS; no local conversation log
- [ ] NVS recent slugs + per-slug bust cache; swipe browses speakers
- [ ] Hardware QA screenshot

## Issue body
`scripts/issue-bodies/faculty-face.md`
""",
        "open",
    ),
    BacklogIssue(
        "round-ui-polish",
        "[Task] Round UI polish (safe area, lower-arc touch)",
        ["enhancement"],
        "P2",
        """## Summary
Safe-area inset and lower-arc touch targets per `docs/pocketwatch.md` experience principles.

## Backlog
`docs/BACKLOG.md` — Round UI polish

## Acceptance criteria
- [ ] Touch targets verified on hardware
""",
        "open",
    ),
    BacklogIssue(
        "low-power-battery",
        "[Feature] Very low power mode on battery",
        ["enhancement"],
        "P1",
        """## Summary
When not USB charging: aggressive low-power after idle (dim/blank AMOLED, stop polling, deep/light sleep). Wake on PWR/BOOT. Normal mode while charging.

## Backlog
`docs/BACKLOG.md` — Very low power mode

## Acceptance criteria
- [ ] Measurable idle draw reduction
- [ ] Wake restores UI + Wi‑Fi
""",
        "open",
    ),
    BacklogIssue(
        "ble-lan-presence",
        "[Feature] BLE or LAN presence with home Mynah",
        ["enhancement"],
        "P2",
        """## Summary
Phase 3: satellite link presence with home Mynah per `docs/pocketwatch.md` phased delivery.

## Acceptance criteria
- [ ] Design doc + MVP protocol
""",
        "open",
    ),
    BacklogIssue(
        "ota-updates",
        "[Feature] OTA firmware updates",
        ["enhancement"],
        "P2",
        """## Summary
OTA updates from GitHub release or custom bucket (TBD).

## Acceptance criteria
- [ ] Signed OTA path documented and tested
""",
        "open",
    ),
    BacklogIssue(
        "koi-pond-face",
        "[Feature] Koi pond ambient clock face",
        ["enhancement"],
        "P2",
        """## Summary
Optional ambient pond animation; modest CPU/GPU budget on ESP32-S3.

## Acceptance criteria
- [ ] Face in swipe cycle; stable frame rate
""",
        "open",
    ),
    BacklogIssue(
        "tibetan-bowl-face",
        "[Feature] Tibetan bowl clock face",
        ["enhancement"],
        "P2",
        """## Summary
Touch circumference to strike bowl tones (I2S); swipe presets; visual ripple; respect face-swipe zones.

## Acceptance criteria
- [ ] Audio + UI on device
""",
        "open",
    ),
    BacklogIssue(
        "sound-mandala-face",
        "[Feature] Sound mandala (audio-reactive) clock face",
        ["enhancement"],
        "P2",
        """## Summary
Mic-driven mandala visualization; bounded FFT cost on ESP32-S3.

## Acceptance criteria
- [ ] Reactive visuals track audio input
""",
        "open",
    ),
    BacklogIssue(
        "alethiometer-face",
        "[Feature] Alethiometer clock face (LLM delight)",
        ["enhancement"],
        "P2",
        """## Summary
36 symbols, 4 hands; STT question → LLM returns needle positions → TTS interpretation. Experimental/delight UX.

## Acceptance criteria
- [ ] JSON contract + on-device animation
""",
        "open",
    ),
    BacklogIssue(
        "iching-face",
        "[Feature] I Ching clock face (LLM delight)",
        ["enhancement"],
        "P2",
        """## Summary
Hexagram cast + STT question → LLM interpretation → TTS. 64-name lookup in flash.

## Acceptance criteria
- [ ] Cast ritual + voice path
""",
        "open",
    ),
    BacklogIssue(
        "mdns-config",
        "[Feature] mDNS + LAN config page (astrolabe-xxxx.local)",
        ["enhancement"],
        "P1",
        """## Summary
`ESPmDNS` hostname; HTTP config UI for NVS settings (birth, synastry profiles, default face, optional Supabase URL). Document in README.

## Acceptance criteria
- [ ] Config UI reachable on LAN
- [ ] NVS persist + README
""",
        "open",
    ),
    BacklogIssue(
        "tls-pinning",
        "[Task] TLS: CA pinning / bundle (replace setInsecure)",
        ["enhancement"],
        "P1",
        """## Summary
Replace `WiFiClientSecure::setInsecure()` with CA pinning or cert bundle.

## Acceptance criteria
- [ ] HTTPS to Supabase without insecure mode
""",
        "open",
    ),
    BacklogIssue(
        "local-timezone",
        "[Task] Local timezone for clock faces (NTP + geo)",
        ["enhancement"],
        "P2",
        """## Summary
Local TZ from NTP + geo or user setting; README currently notes UTC-only.

## Acceptance criteria
- [ ] Faces show local civil time when configured
""",
        "open",
    ),
    BacklogIssue(
        "tts-hardware-readme",
        "[Task] README: confirm TTS path for Waveshare SKU",
        ["enhancement"],
        "P2",
        """## Summary
Document codec, amp, and speaker path for pinned Waveshare board in README.

## Acceptance criteria
- [ ] README hardware audio section accurate
""",
        "open",
    ),
    BacklogIssue(
        "wake-word-deferred",
        "[Task] Wake word explicitly deferred (PTT only v1)",
        ["enhancement"],
        "P2",
        """## Summary
Wake word out of scope for v1; push-to-talk only. Track as deferred decision.

## Acceptance criteria
- [ ] Documented in README/backlog as wontfix for v1
""",
        "open",
    ),
    BacklogIssue(
        "circadian-hue-full-keyframes",
        "[Task] Circadian hue: full 12-stop keyframes + QA",
        ["enhancement"],
        "P1",
        """## Summary
Expand `pm_circadian_hue` from 5-stop MVP to full BACKLOG stops (250° midnight … 270° night). Port Astrolabe sketch paths. QA screenshots at 00:00, 06:00, 12:00, 18:00 on hue home.

## Related
Closes gap after #2 (basic keyframes landed).

## Acceptance criteria
- [ ] All keyframe stops implemented
- [ ] Hardware QA screenshots at four times
""",
        "open",
    ),
    BacklogIssue(
        "astrology-transits-remaining",
        "[Feature] Astrology face: aspects, chrome trim, ephemeris accuracy",
        ["enhancement"],
        "P1",
        """## Summary
Follow-up to #3: aspect lines, trim footer chrome, ephemeris server accuracy when available.

## Related
#3 (initial polish closed)

## Acceptance criteria
- [ ] Aspect rendering (TBD design)
- [ ] Hardware QA on astrology face
""",
        "open",
    ),
    BacklogIssue(
        "commonplace-deploy-verify",
        "[Task] Commonplace: deploy edge fn + on-device verify",
        ["enhancement"],
        "P0",
        """## Summary
Operator tasks for commonplace journal: `supabase functions deploy mynah-pocket-journal`, Castalia sign-in, on-device save verify.

## Related
Firmware path in commonplace-journal issue.

## Acceptance criteria
- [ ] Function deployed
- [ ] End-to-end save from watch
""",
        "open",
    ),
    BacklogIssue(
        "spotify-vinyl-queue-design",
        "[Feature] Mynah Spotify Face (Vinyl Queue): design doc and epic",
        ["enhancement"],
        "P1",
        """## Summary
Add canonical **Mynah Spotify Face** (“Vinyl Queue”) design documentation and a BACKLOG epic. Core UX: **swipe explores, tap commits** — vertical album-art stream with center vinyl record; `selected_track` vs `playing_track`; hub-built Music Stream and preprocessed album art.

Extends the native `astrolabe175c/main/faculty175_face_spotify.c` implementation with the full interaction and visual model.

## Design doc
`docs/mynah-spotify-face.md` (sections 1–30: UX, state machine, payloads, milestones, v1/v2 scope)

## Acceptance criteria
- [ ] `docs/mynah-spotify-face.md` committed
- [ ] `docs/BACKLOG.md` — **Music / Spotify face** epic with M1–M5 child items
- [ ] `./scripts/astrolabe175c_build.sh build` passes when firmware changes

## Implementation milestones (follow-on issues or subtasks)
| # | Milestone |
|---|-----------|
| M1 | Static mock face (layout, fake art, browse vs now-playing visuals) |
| M2 | Gesture prototype on device |
| M3 | Hub `spotify_state` + cached art + commands |
| M4 | Spotify Music Stream + playback integration (mynah hub) |
| M5 | Polish: hue ring, glint, Commonplace, QA screenshot |

## Priority
P1

## Backlog reference
`docs/BACKLOG.md` — Music / Spotify face (“Vinyl Queue”)

## Related code
- `astrolabe175c/main/faculty175_face_spotify.c`
- Castalia `mynah-spotify` service

## Notes
Parent epic for Vinyl Queue. Horizontal swipe stays reserved for face changes. v1 excludes lyrics, ESP32 OAuth, and browse previews.
""",
        "open",
        72,
    ),
]


def gh_json(*args: str) -> dict:
    cmd = ["gh", "api", f"repos/{REPO}/issues", *args]
    out = subprocess.check_output(cmd, text=True)
    return json.loads(out)


def create_issue(item: BacklogIssue) -> int:
    if item.existing:
        return item.existing
    labels = ",".join(item.labels)
    body_path = f"/tmp/backlog-issue-{item.key}.md"
    with open(body_path, "w", encoding="utf-8") as f:
        f.write(item.body)
    cmd = [
        "gh",
        "issue",
        "create",
        "--repo",
        REPO,
        "--title",
        item.title,
        "--body-file",
        body_path,
        "--label",
        labels,
    ]
    out = subprocess.check_output(cmd, text=True).strip()
    num = int(out.split("/")[-1])
    if item.state == "closed":
        subprocess.check_call(
            ["gh", "issue", "close", str(num), "--repo", REPO, "--reason", "completed"]
        )
    time.sleep(0.4)  # rate limit courtesy
    return num


def main() -> None:
    mapping: dict[str, int] = {}
    for item in ITEMS:
        num = create_issue(item)
        mapping[item.key] = num
        print(f"{item.key}\t#{num}\t{item.state}\t{item.title}", flush=True)
    print("\n--- JSON ---")
    print(json.dumps(mapping, indent=2))


if __name__ == "__main__":
    main()
