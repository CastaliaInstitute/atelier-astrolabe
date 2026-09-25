# Requirements: Voice-Stream Codec (µ-law over WiFi + spool)

Extends the Castalia `voice-stream` protocol and the device flash spool so both share the
µ-law codec already used on the BLE bridge path. PCM16 remains the default on all WiFi paths.
Server-side (Edge Function) implementation lives in the mynah repo; this spec authors the
protocol contract and firmware-side capture-spool details. Opus is explicitly OUT of the WiFi
path (BLE-reserved codec row 20).

## Requirements

### Requirement: Socket frame-type

THE voice-stream binary frame format SHALL add a `0xA3` frame type with the same header as
`0xA1`, carrying µ-law 16 kHz mono payload bytes:
`0xA3 <uint32 LE sequence> <uint32 LE capture_ms> <u-law bytes>`.
- WHEN either frame type arrives, the Castalia endpoint SHALL decode/normalize to PCM16 16k
  mono before buffering/commit (µ-law decode is an exact 256-entry table; no path-specific
  STT behavior differences introduced).
- IF a µ-law payload's byte count is odd, THEN the endpoint SHALL drop the trailing byte and
  count it (same tolerance rule as the device playout path).
- THE `0xA1` PCM16 frame SHALL remain the default and SHALL keep priority in QA paths
  (`voice-pipeline` HTTP fallback NEVER accepts µ-law).

### Requirement: Session opt-in

WHEN a `session.update` sets `"input":{"format":"ulaw16"}`, THE ENDPOINT SHALL accept `0xA3`
frames and tag the session's spool/archive records as µ-law; WHEN absent, PCM16 is assumed
(today's behavior, byte-identical).

### Requirement: Flash spool codec

THE device flash spool (per `streaming-audio-socket.md` §Firmware Architecture) SHALL store
µ-law records at half the byte rate of PCM16 (16 kHz mono), doubling effective spool capacity.
- Spool metadata SHALL flag the record codec (µ-law default going forward; legacy PCM16
  segments remain readable during rollout).
- ON spool replay to the endpoint, the device/app SHALL send `0xA3` frames with original
  capture cadence so `capture_ms` deltas stay truthful (elapsed-time semantics per sample rate
  documented in the contract §5 note).

### Requirement: Consistency with the BLE bridge

THE socket-side codec ids SHALL mirror the BLE contract row ids (µ-law 16 = row 10, PCM16 =
row 0) so the Mynah relay's normalizing transcoder produces the same bytes the endpoint stores.

### Requirement: Mynah side (cross-repo work)

THE Edge Function changes (accept 0xA3, µ-law→PCM16 decode, session format opt) SHALL be
flagged for implementation in `../mynah` (`supabase/functions`), spec-authored here; no astrolabe
repo code is blocked by it. THE contract doc SHALL note the endpoint phase order: accept both
frame types first, session opt second, spool replay third.

### Requirement: Verification

THE contract doc §5 SHALL gain the µ-law-over-socket note AND
`docs/design/streaming-audio-socket.md` SHALL encode the new frame type, opt message, and spool
codec rules; a doc-only spec SHALL be reviewable without any code.
