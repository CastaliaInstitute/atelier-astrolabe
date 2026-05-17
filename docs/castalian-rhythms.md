# Castalian Rhythms

## Summary

**Castalian Rhythms** is Astrolabe's on-device-first astrology and daily rhythm layer. V0 should produce a compact, glanceable card on the ESP32 watch from local time, stored birth/profile data, a firmware ephemeris, an embedded interpretation knowledge base, and deterministic fusion rules.

The watch may use Castalia services when available, but V0 must not require Mynah or a server to produce the daily card. Remote ephemeris and LLM services are optional accuracy and prose upgrades layered on top of a complete local path.

## Architecture

```text
NTP / RTC time
  + observer location
  + birth profile(s) in NVS
        |
        v
local ephemeris and chart math
        |
        v
embedded interpretation KB
        |
        v
firmware fusion / scoring
        |
        +--> compact daily card cache
        +--> Astrology BOOT brief
        +--> daily card clock face
```

### Firmware responsibilities

- **Profile store:** Keep birth date/time, optional birthplace/location, observer location, and named chart profiles in NVS. Missing birth time should be explicit and should fall back to a documented default such as local noon when a feature can tolerate lower precision.
- **Local ephemeris:** Use firmware math for Sun, Moon, and major planet longitudes, plus house and aspect approximations where feasible. The local path must be good enough to make a coherent compact card while offline.
- **Embedded KB:** Ship a small versioned knowledge base in firmware or generated headers. Entries should be structured by symbol type (planet, sign, house, aspect, transit), tags, short phrases, and safety notes instead of free-form prompts.
- **Fusion layer:** Combine natal placements, current transits, aspects, lunar/circadian context, and KB snippets into a bounded card. Rules should be deterministic, cheap to compute, and testable with fixed timestamps/profile fixtures.
- **Presentation:** Expose the result as a compact daily card face and as the Astrology face BOOT brief. The UI should favor one or two useful themes, a visible confidence/precision cue when data is approximate, and no long scrolling feed.

### Optional service responsibilities

- **Castalia ephemeris server:** Optional accuracy upgrade for higher precision natal data, houses, synastry, and future chart features. It can replace or refine local approximations when online, but the Rhythms V0 compact card must still work without it.
- **Castalia voice / LLM services:** Optional prose expansion, Q&A, journaling, or faculty-style interpretation. V0 card generation should not depend on remote generation.
- **Mynah app / home device:** Optional companion for account, richer reading, or profile management. Astrolabe remains capable of generating its own V0 card locally.

## Data layers

| Layer | Examples | Storage | V0 requirement |
|-------|----------|---------|----------------|
| Device context | UTC time, local timezone, observer lat/lon, battery/network state | RTC, NTP, NVS | Required |
| Profile context | User birth date/time/place, optional named profiles | NVS | Required for natal features |
| Ephemeris facts | Longitudes, houses, aspects, lunar phase, transit windows | Computed locally; optional cached server result | Required locally |
| Interpretation KB | Short meanings, tags, weights, caveats, templates | Embedded firmware asset | Required |
| Fusion output | Daily title, themes, symbols, confidence notes, generated-at timestamp | RAM + optional NVS cache | Required |
| Remote enrichments | High precision charts, LLM prose, journal logging | Castalia APIs | Optional |

Prefer structured records over prompt strings so firmware can select, rank, and render content without a network call. If KB assets are generated from source data, keep the source and generator deterministic so changes are reviewable.

## Privacy and safety

- Birth data, observer location, chart profiles, and generated cards stay on device by default.
- Do not upload birth/profile data in the background. Network calls require an explicit feature path, a user action, or a documented online refresh.
- Use the existing Castalia JWT flow only when remote services are requested; never commit secrets or device-specific credentials.
- The card is reflective and interpretive, not medical, legal, financial, or deterministic fate guidance.
- Copy should avoid certainty about harm, illness, relationships, or irreversible decisions. Prefer invitations such as "notice", "consider", and "good moment for" over commands.
- Surface approximation honestly: unknown birth time, missing location, offline mode, or local ephemeris fallback should influence confidence text.

## V0 scope

V0 is the smallest complete on-device path:

1. Local natal, house, and transit-aspect facts for one primary profile.
2. Embedded interpretation KB v0 with compact symbols and phrases.
3. Firmware fusion that builds a daily card without remote generation.
4. Daily card clock face plus Astrology BOOT brief using the cached/latest card.

Tracked implementation issues:

- [#62](https://github.com/CastaliaInstitute/astrolabe/issues/62) — on-device natal, houses, transit aspects.
- [#63](https://github.com/CastaliaInstitute/astrolabe/issues/63) — embedded interpretation KB v0.
- [#64](https://github.com/CastaliaInstitute/astrolabe/issues/64) — on-device fusion and compact daily card.
- [#65](https://github.com/CastaliaInstitute/astrolabe/issues/65) — daily card clock face and Astrology BOOT.

Out of V0 unless needed by those issues: server-primary chart generation, Mynah-primary card generation, multi-profile synastry, long-form LLM readings, cloud KB fetches, and automatic journaling.

## Upgrade path

After V0 is reliable offline, online upgrades can refine the same data model:

- Replace local planetary/house approximations with a signed Castalia ephemeris response when available.
- Add richer prose or follow-up questions through Castalia voice/LLM routes.
- Sync profile management with Mynah or a LAN config page.
- Expand from one daily card to synastry, family profiles, or longer readings while preserving local fallback behavior.
