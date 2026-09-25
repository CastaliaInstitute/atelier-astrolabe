# Requirements: BLE Audio TTS Playback (firmware)

Wires the TTS write characteristic (0x12) and buffer-credit characteristic (0x15) into real
playback on both `astrolabe175c/main` and `astrolabe185b/main`, per
`docs/design/ble-audio-bridge.md` §3, §5–§6. Mic streaming (spec 2) is untouched.

## Requirements

### Requirement: TTS write ingestion

WHEN a write-without-response arrives on char 0x12, THE SYSTEM SHALL parse the 4-byte header
`[seq LE][codec][frag][payload]`, validate length ≥ 4 and codec ∈ {0,10,11}, copy the payload
into the playout ring ONLY if free space ≥ payload size (otherwise count `dropped` and
overwrite-oldest per contract §6), and return success.
WHEN TTS writes are arriving, THE SYSTEM SHALL leniently track gaps (never stall, never
reorder).

### Requirement: Playout

WHEN the playout ring has ≥ 320 bytes primed after an idle drain (≈10 ms of PCM16k), THE SYSTEM
SHALL start/continue consuming chunks: decode per chunk codec byte (0 passthrough; 10/11 µ-law
decode to PCM16 16k/8k), then feed `faculty175_audio_write_pcm` with skip-underflow behavior.
IF the board speaker route is not ready, THEN the playout SHALL drop that audio and count it
(mic-only safety; never block the BLE write path).

### Requirement: Credit notifications

THE SYSTEM SHALL notify char 0x15 with `u32 LE free bytes` WHEN free space crosses each 12.5 %
level of ring size, and additionally every 500 ms WHILE the app has subscribed the 0x15 CCCD.
ON link disconnect, credit notifications SHALL stop.

### Requirement: Codec mismatch

IF an ingested chunk's codec byte differs from the row the mic direction negotiated, THEN the
chunk SHALL still decode per its own byte (the per-chunk codec is authoritative).

### Requirement: Compatibility and budgets

THE SYSTEM SHALL NOT regress mic streaming behavior (spec 2 counters/tests still hold), SHALL
keep the playout task stack ≤ 4 kB and idle-wait only, and SHALL re-use the existing speaker
mute/PA/volume controls (`faculty175_audio_set_speaker_mute` etc.) rather than bypassing them.

### Requirement: Verification

MODULE copies SHALL stay byte-identical (shasum) across variants; both variant builds SHALL
succeed; `qa blemic` SHALL additionally print TTS state (written/dropped/underflows/credit
value) for operator device QA.
