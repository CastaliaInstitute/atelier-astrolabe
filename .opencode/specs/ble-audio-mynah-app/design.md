# Design: BLE Audio — Mynah App Bridge (repo `../mynah`)

New Kotlin files under `android/app/src/main/java/institute/castalia/mynah/ble/`, building on
the existing BLE seams; astrolabe repo is spec-author only.

## Module layout

| New file | Role |
|---|---|
| `ble/AstrolabeAudioClient.kt` | GATT client: connect/disconnect, service/char discovery, codec+credit reads, notify subscriptions |
| `ble/AstrolabeAudioReassembler.kt` | seq wrap-aware chunk reassembly (stateless codec step per chunk; forward → relay) |
| `voice/AstrolabeVoiceStreamRelay.kt` | WSS client to `voice-stream` (Castalia JWT headers via `bearerForEdgeFunctions()` parity), binary 0xA1 frames out, JSON events out/in |
| `tts/AstrolabeTtsTranscoder.kt` | `response.audio` (MP3/PCM) → decode (existing TTS decode path shared with device MP3 playback code) → codec encode (µ-law LUT for row 10; PCM16 passthrough) → chunked BLE writes honoring credit |
| `ble/AstrolabeAudioService.kt` | foreground service (`connectedDevice`), owns client lifecycle, watchdog timer, failure-enum state transitions |

## Key flows

**Streaming session**: UI triggers service (session scope from GlowScreen voice route →
`session.update` format echo) → service connects GATT → subscribes 0x11+0x15 → reads codec
(0x13) + credit (0x15 initial) → opens `voice-stream` → pumps reassembled chunks as A1/A3
binary frames → relays `input_audio_buffer.speech_*`/`transcript` events to session UI.

**Watchdog** (`AstrolabeAudioService`): 4 s timer armed on $reconnect; reset by any 0x11
chunk; on timeout force CCCD subscribe once; second failure → `LINK_LOSS_RESUMED`/`LINK_LOST`
post + session pause.

**TTS relay**: credit state machine (local credit, decrement per write, refresh on notify;
initial read at run start) inside `AstrolabeTtsTranscoder`; write-without-response queued via
BLE API write batch; honor reconnect re-reads (`Requirement: Resume semantics`).

## Compat / non-goals

- Not touching `WifiCredentialVault` / hotspot provisioning paths; Castalia auth rides the
  phone's existing session (mynah repo ownership).
- Watch connectivity: out of scope (Apple Watch integration is a mynah-side later spec).
- iOS: this milestone is Android-first — the family's only app today; the BLE protocol is
  platform-agnostic so an iOS relay can adopt the same contract later.
- No gap backfill on resume (live-speech tolerance), spool pull deferred.

## Verification

`../mynah` device tests: connect → subscribe → chip-level relay round trip; credit
envelope holds under sustained TTS; watchdog fires once max per reconnect; Castalia sees
identical commits as the device-direct QA path (the `voice-pipeline` comparison baseline).
