# Requirements: BLE Audio — Mynah App Bridge

Implementation repo: **`../mynah`** (Android app under
`android/app/src/main/java/institute/castalia/mynah/`). This spec is authored in the astrolabe
repo as the client-side contract to `docs/design/ble-audio-bridge.md`; astrolabe firmware and
the socket contract are inputs, never outputs here.

## Requirements

### Requirement: GATT audio client role

THE app SHALL add a second GATT client role for the Astrolabe Audio Service:
- Discover/connect by presence advertisement (`AstrolabePresenceCodec.kt`) + name; audio
  service UUID filter optional (31-byte adv constraint, contract §2).
- ON connect: read codec char (0x13) and credit char (0x15); subscribe mic notify (0x11) and
  credit notify (0x15).
- THE app SHALL reassemble mic chunks by (seq wrap-aware, frag) and forward raw payload bytes
  as `0xA1`/`0xA3` binary frames to the Castalia `voice-stream` WSS, choosing frame type by
  the codec row (0 → 0xA1, 10/11 → decode to PCM16 16k first, then `0xA1`; keep row id
  consistency per voice-stream-codec).
- IF no subscriber is possible (BT off / permission / link lost), THE APP SHALL surface the
  contract §11 failure enum to the session UI.

### Requirement: Castalia credentials

THE app SHALL hold the Castalia JWT (same model as `VoicePipelineClient.kt`); THE DEVICE SHALL
NOT be sent any Supabase credentials over BLE. THE relay SHALL keep the socket event protocol
from `streaming-audio-socket.md` (binary input frames, JSON control events, commit/turn ids).

### Requirement: Foreground service

WHEN a streaming session is active, THE app SHALL run it in a foreground service
(`connectedDevice` type) so the OS does not kill the socket or CCCD subscription. THE app SHALL
tie BLE client lifecycle to that service's lifetime (create/destroy on session start/stop).

### Requirement: CCCD liveness watchdog

IF no mic bytes arrive ≤ 4 s after a reconnect while the subscription state claims active,
THEN THE app SHALL subscribe the 0x11 CCCD once more; IF that also fails, THEN THE app SHALL
surface `LINK_LOSS_RESUMED`/`LINK_LOST` instead of silently stalling (contract §9.4).

### Requirement: TTS relay

WHEN the socket delivers reply audio (MP3/PCM per `response.audio` events), THE app SHALL
transcode to the ACTIVE codec of the device (codec char read on (re)connect; per-chunk codec
byte is authoritative) and stream it to char 0x12 as write-without-response honoring char 0x15
credit: never exceed last-known credit, refresh on credit notify, initial credit read at TTS
run start.

### Requirement: Resume semantics

ON link loss and reconnect, THE app SHALL re-read codec + credit, resume mic relay without
gap backfill (live speech), and queue a `LINK_LOSS_RESUMED` event for UI. Spool backlog pull
is out of scope (contract §10 notes it as follow-up firmware protocol).

### Requirement: Verification

THE app-side implementation SHALL be testable from `../mynah` against a device running the
spec-2/3 firmware: nRF Connect chip-level sanity, then the Mynah relay flow; acceptance =
contract §12 round-trip (≈1.5–1.7 s conversational turn) on 175c hardware.
