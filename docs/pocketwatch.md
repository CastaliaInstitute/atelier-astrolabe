> **Astrolabe:** Exported from mynah `docs/design/pocketwatch.md` at repo split. Firmware paths refer to this repository root.

# Mynah Pocketwatch

## Summary

**Mynah Pocketwatch** is a **wearable / pocket round display** (reference class: Waveshare **ESP32-S3** 1.75″ AMOLED touch, 466×466) that runs a **focused Mynah experience**: glanceable time and status, short voice or text turns with the **same Castalia Supabase Edge Functions** as the Android Mynah app, and optional **proximity to the home Mynah** (BLE advertisement awareness or LAN).

It is an **accessory / satellite** to the primary [mini-smartphone Mynah platform](https://github.com/CastaliaInstitute/mynah/blob/132f889a3fff927499f3a8625bdffa159da39ec9/docs/design/hardware-platform.md): the **household object** remains a **portrait rectangle**; the pocketwatch is explicitly **round** and **secondary**.

## Hardware reference (Tier 1)

| Attribute | Reference |
|-----------|-----------|
| MCU | ESP32-S3 (dual-core, Wi‑Fi + BLE) |
| Display | ~1.75″ **round** AMOLED, capacitive touch, **466×466** |
| Audio in | Onboard mic array (vendor stack) |
| Audio out | Per SKU (speaker / I2S codec — confirm on schematic) |
| Power | USB-C, battery connector / PMIC (e.g. AXP class) |
| Dev baseline | Vendor Arduino / ESP-IDF examples and [Waveshare wiki / samples](https://www.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-1.75) |

Exact **SKU** (e.g. **1.75** vs **1.75C** variant), **speaker pinout**, and **PSRAM / flash** line-up should be pinned in [`README.md`](../README.md) once a board is fixed for QA.

## Software architecture

### Backend (reuse)

Same project as Android Mynah:

| Capability | Edge function | Client contract |
|------------|----------------|------------------|
| Voice → reply + MP3 | `voice-pipeline` | `POST` JSON: optional `audioBase64` (16 kHz mono PCM), optional `message`, `languageCode`, optional `skipLlm`; `Authorization: Bearer …` (Castalia JWT when available) and/or `apikey` (Supabase anon). Response includes `audioBase64` (MP3) and text fields. |
| Faculty-flavored ask + TTS | `ask-faculty` | Used directly or via `voice-pipeline` routing (see [`supabase/functions/voice-pipeline/index.ts`](https://github.com/CastaliaInstitute/mynah/blob/132f889a3fff927499f3a8625bdffa159da39ec9/supabase/functions/voice-pipeline/index.ts)). |
| Faculty portrait asset | `faculty-bust` | `GET` with `faculty` query param. |

**Secrets:** Google keys stay on the server. The watch ships **Supabase URL + anon key** at minimum (same model as [`VoicePipelineClient.kt`](https://github.com/CastaliaInstitute/mynah/blob/132f889a3fff927499f3a8625bdffa159da39ec9/android/app/src/main/java/institute/castalia/mynah/voice/VoicePipelineClient.kt)); optional **user session JWT** for parity with Android `bearerForEdgeFunctions()`.

### Firmware (this repo)

Firmware source lives at this **repository root** (**PlatformIO** + **Arduino**); **ESP-IDF** is acceptable if we need tighter audio/display coupling.

**Suggested layers:**

1. **Shell** — Wi‑Fi provisioning (captive portal or hard-coded dev secrets), NTP time, deep sleep / wake.
2. **UI** — Round **LVGL** (or vendor layer): watch face, transcript snippet, faculty bust thumbnail, minimal chrome.
3. **Audio** — Capture PCM **16 kHz mono** for `voice-pipeline`; decode MP3 reply (I2S / software decoder per BOM).
4. **Optional link to home Mynah** — Parse BLE manufacturer / service data compatible with [`MynahBleCodec`](https://github.com/CastaliaInstitute/mynah/blob/132f889a3fff927499f3a8625bdffa159da39ec9/android/app/src/main/java/institute/castalia/mynah/ble/MynahBleCodec.kt) **or** HTTP to LAN dashboard URLs if the phone exposes them (future contract).

## Experience principles

1. **Glance first** — Default surface is **time + one-line status** (e.g. “Home glow: dusk”). No feed scrolling as the primary mode.
2. **Short turns** — Prefer **one** voice exchange or **one** typed line; long answers should **truncate on screen** with optional “read more on Mynah” (out of scope until phone handoff exists).
3. **Round layout** — Safe area inset from the circular bezel; touch targets along the **lower arc** for thumb reach.
4. **Battery honesty** — Aggressive sleep between interactions; show **low battery** before hard cutoff.

## Phased delivery

| Phase | Outcome |
|-------|---------|
| **0** | Toolchain + serial “shell”; secrets template; flashes on reference ESP32-S3 class board. |
| **1** | Wi‑Fi + `voice-pipeline` **text-only** (`message` in, MP3 + text out); on-device MP3 play if hardware allows. |
| **2** | Round watch UI + **mic path** to `audioBase64`; faculty bust + `ask-faculty` routing parity with Android voice flows. |
| **3** | BLE / LAN **presence** with home Mynah; OTA updates (GitHub or custom bucket TBD). |

## Open questions

- **Auth:** Ship anon-only vs require Castalia login flow on-watch (WebView / companion app BLE pairing).
- **TTS playback:** Confirm output path (codec, amp, speaker) for the pinned SKU.
- **Wake word vs push-to-talk:** PTT is simpler for v1; wake word is a later milestone.

## Revision history

| Date | Change |
|------|--------|
| 2026-05-15 | Synced firmware from mynah `pocketwatch/` at [`132f889`](https://github.com/CastaliaInstitute/mynah/commit/132f889a3fff927499f3a8625bdffa159da39ec9); astrolabe doc links use that SHA. |
