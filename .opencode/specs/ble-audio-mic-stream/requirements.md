# Requirements: BLE Audio Mic Stream (firmware)

Implements `docs/design/ble-audio-bridge.md` on both `astrolabe175c/main` and
`astrolabe185b/main`. TTS playback (char 0x12 write handler) is registered but stubbed;
`ble-audio-tts-playback` fills it.

## Requirements

### Requirement: Audio service registration

THE SYSTEM SHALL register the Audio primary service (UUID trailing byte `0x10`) with
characteristics `0x11` mic (READ+NOTIFY+CCCD), `0x12` tts write (WRITE_WO_RSP, handler stubbed
to consume-and-discard), `0x13` codec (READ), `0x14` stream control (WRITE w/ response), `0x15`
credit (READ+NOTIFY+CCCD) exactly as §3 of the contract defines, byte-identically in both
variant firmware trees, shipping the same new source file(s) in each.

### Requirement: Mic streaming

WHEN the app subscribes the 0x11 CCCD AND stream control receives `{"mic":1}`, THE SYSTEM SHALL
begin streaming within 250 ms at the negotiated codec cadence (chunk sizes per §5 of the
contract), with the 4-byte header `[seq LE][codec][frag=0][payload]`; WHEN either condition
reverts, THE SYSTEM SHALL stop streaming chunks.
THE seq counter SHALL wrap monotonically.

### Requirement: Codec negotiation

AFTER the peer's MTU state changes or per connect, THE SYSTEM SHALL select the highest-quality
codec row whose full notification fits MTU−3 and SHALL update the codec characteristic; V1 rows
are only `0` (PCM16 16k), `10` (µ-law 16k), `11` (µ-law 8k).

### Requirement: TX budget

THE SYSTEM SHALL gate in-flight audio notifications so that at most (N−2) audio mbufs are
outstanding where N is the configured NimBLE msys pool size for the audio pool, leaving control
notifications unstarved (battery, credit, motion, settings JSON).

### Requirement: Idle duty

WHILE streaming is off, THE SYSTEM SHALL NOT read the microphone for streaming tasks
(state machine quiescent, no capture loop spin).

### Requirement: Arbitration integration

WHEN the Colmi-ring client transitions into an active scan/connect window, THE SYSTEM SHALL stop
mic chunking and the stream-control read SHALL report `arb:"ring"`; it SHALL resume streaming on
exit of that window (no gap retransmit). THE SYSTEM SHALL retain the existing serial-activity
deferral behavior (`faculty175_ble_serial_activity`) unaffected.

### Requirement: Spool on loss

IF the mic is capturing (turn in progress) when the BLE link drops, THEN frames SHALL continue to
the flash spool ring specified by `docs/design/streaming-audio-socket.md` §Firmware Architecture
(capture-only; V1 does not re-sync spool over BLE).

### Requirement: Compatibility

THE SYSTEM SHALL NOT alter the existing GATT table behavior (settings/state/health/control JSON,
battery, Colmi client, presence advertisement content) beyond appending the audio service entry
and adding the advertisement UUID list entry per contract §2.

### Requirement: Task budgets

THE packing task SHALL consume ≤ 5 % steady CPU at codec row 0 (160-sample/s16 capture +
chunk packing + notify), priority above the render loop and below critical control tasks; no
busy-wait polling.

### Requirement: Verification

Builds for both variants SHALL pass (`idf.py build` or repo build scripts), the two audio-source
copies SHALL be byte-identical (shasum check), and a serial QA hook `qa blemic` SHALL report
streaming state, seq wrap count, chunks sent, and dropped-notify counter for operator device QA.
