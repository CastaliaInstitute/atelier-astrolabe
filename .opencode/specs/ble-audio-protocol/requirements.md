# Requirements: BLE Audio Protocol Contract

## Overview

Astrolabe devices make BLE (to the Mynah Android app, repo `../mynah`) the **primary** connectivity
for voice. Devices do not call Castalia directly over WiFi for audio; the Mynah app relays mic
audio to the Castalia `voice-stream` socket and relays TTS audio back over BLE. WiFi remains for
OTA only. This spec produces the cross-repo wire contract `docs/design/ble-audio-bridge.md`
consumed by firmware specs (`ble-audio-mic-stream`, `ble-audio-tts-playback`) and the Mynah-side
app spec authored for `../mynah`.

Downstream firmware targets BOTH variants `astrolabe175c/main` and `astrolabe185b/main`.

Out of scope: firmware implementation, app implementation, OTA changes, Opus support (V1 codecs
are PCM16 and µ-law; Opus codec id `20` is reserved for later).

## Requirements

### Requirement: Service map

THE SYSTEM SHALL define a BLE Audio GATT service with these characteristics, extending the
in-repo Astrolabe UUID base used in `faculty175_ble.c` (`41 73 74 72 6f 6c 61 62 65 00 17 50 00 00 00 XX`,
little-endian init of ASCII "Astrolabe\0\x17P"):

| Char trailing byte | Name | Flags | Purpose |
|---|---|---|---|
| 0x11 (service base 0x10) | mic audio data | read + notify + CCCD | mic chunks device → app |
| 0x12 | tts audio data | write-without-response | TTS chunks app → device |
| 0x13 | codec | read | 1-byte codec id of the active stream |
| 0x14 | stream control | write (with response) | time sync +mic start/stop JSON |
| 0x15 | buffer credit | read + notify + CCCD | TTS playout-buffer byte credit, u32 LE |

WHEN the app reads the codec characteristic, THE SYSTEM (contract) SHALL return one byte:
`0` = PCM16 16 kHz mono LE, `1` = PCM16 8 kHz mono LE, `10` = µ-law 16 kHz, `11` = µ-law 8 kHz.
THE device SHALL set the codec byte in every audio chunk so mid-stream codec changes need no
renegotiation.

### Requirement: Packet framing

-WHEN an audio chunk is sent in either direction, THE SYSTEM SHALL use self-contained per-
notification chunks with header `[uint16 seq LE][uint8 codec id][uint8 fragment index][payload]`,
where payload ≤ MTU−3 and seq is a monotonically increasing per-nXP (wrapping) counter and
fragment index is 0 for single-notification payloads.
IF a fixed codec frame (10 ms) exceeds MTU−3, THEN THE SYSTEM SHALL split into fragments indexed
from 1 upward, and receivers SHALL reassemble by (seq round-down, fragment count) and SHALL
advance (skip) forward rather than stall on a gap.
-BECAUSE chunks are self-contained decodable units, THE SYSTEM SHALL tolerate dropped
notifications in both directions for continuous speech (STT) and TTS glitch tolerance.

### Requirement: Connection behavior

WHEN the app subscribes to the mic CCCD and enables mic via stream control, THE DEVICE SHALL
begin streaming within 250 ms (contract requirement on firmware).
WHILE no subscriber is present, THE DEVICE SHALL NOT run the mic capture task (idle duty).
THE CONTRACT SHALL require the audio link to achieve MTU ≥ 247 (app-side MTU request; firmware
advertises its preferred MTU) and use DLE where both sides support it, and to reserve ≥ 1 TX slot
for control characteristics (battery/settings/motion) so control notifications are never starved
while streaming.
WHILE a Colmi-ring scan/connect is active, THE DEVICE SHALL pause mic streaming and advertise the
pause via stream control (`{"arb":"ring"}`); THE CONTRACT SHALL document this single-link
arbitration.

### Requirement: Time sync

ON connection, THE Mynah app SHALL write 0x01 + 4-byte epoch seconds (LE) to the stream control
characteristic so the watch corrects its clock without NTP while paired.

### Requirement: Mynah app contract

THE document SHALL specify, for the Mynah Android app in `../mynah`
(`android/app/src/main/java/institute/castalia/mynah/ble/` — presence already handled by
`AstrolabePresenceCodec.kt`; `MynahBleControlService.kt`/`MynahBleScanner.kt` are the existing
BLE seams):
- Discover/connect using the astrolabe advertisement; subscribe to mic CCCD; reassemble chunks by
  (seq, fragment) and forward to the Castalia `voice-stream` socket from
  `docs/design/streaming-audio-socket.md` (auth via Castalia JWT on the phone only).
- Sustained streaming runs in a foreground service.
- CCCD liveness watchdog: if no mic bytes ≤ 4 s after reconnect, force one re-subscribe.
- TTS relay: app transcodes reply audio (MP3 or PCM) to the negotiated codec, writes chunks
  write-without-response honoring the buffer-credit notify.
- Reconnection semantics that resume streaming; user-visible failure states queued for UI.

### Requirement: Firmware contract

THE document SHALL specify, for firmware specs 2 and 3:
- Mic source `faculty175_audio_read` (16 kHz mono s16, ES7210) and speaker sink
  `faculty175_audio_write_pcm` (ES8311), both already variant-independent.
- Coexistence rules with the existing `faculty175_ble.c` GATT table, Colmi scan/advertise duty,
  and the serial/ring control state machine.
- Budgets: audio task priority, worst-case CPU share, and expected ~1–2 s conversational
  latency (documented cap, not exceeded by design).

### Requirement: Traceability and review

THE document SHALL cite the Omi protocol reference (docs.omi.me Protocol; BasedHardware/omi
`omi/firmware/omi/src/lib/core/transport.c` for notify-header/throttle patterns), the in-repo
seams (`faculty175_ble.c`, `faculty175_board.c`, `faculty175_voice.h`) in both
`astrolabe175c/main` and `astrolabe185b/main`, and `docs/design/streaming-audio-socket.md`.
THE document SHALL state it supersedes device-direct socket audio ONLY while BLE is the active
link; the socket remains the Castalia-side endpoint.

### Requirement: Variant parity (175c + 185b)

THE protocol SHALL be board-agnostic and byte-identical across variants.
THE audio service SHALL be implemented identically in both `astrolabe175c/main/faculty175_ble.c`
and `astrolabe185b/main/faculty175_ble.c`, whose GATT tables are already duplicated; the shared
seam is `faculty175_board.h`, never variant pins/codecs.
IF a variant lacks a building block (e.g. no speaker route), THEN the contract SHALL specify
which characteristics remain functional and the codec char SHALL still report the mic direction
codec so the app degrades gracefully (mic-only mode).
THE contract SHALL NOT branch on variant; variant identity MAY be read via existing state JSON
characteristics.

### Requirement: Code-first contract

THE contract document SHALL be written before any implementation: all decisions herein refer to
protocol/format only, and downstream specs SHALL be implementable from this contract plus repo
context alone.
