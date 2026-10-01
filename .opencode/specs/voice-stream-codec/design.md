# Design: Voice-Stream Codec

Doc-only spec; two documents change, no firmware/server code here.

## 1. `docs/design/streaming-audio-socket.md` edits

- **Client Events (binary frames)**: add `0xA3` alongside `0xA1`:
  `0xA3 <uint32 LE sequence> <uint32 LE capture_ms> <u-law bytes>` with the rule that the
  endpoint normalizes u-law → PCM16 16k mono before buffering; odd-byte tolerance = drop last
  byte + counter. Session opt-in: `{"type":"session.update","session":{"audio":{"input":{"format":"ulaw16"}}}}`
  (absence = PCM16, unchanged). `voice-pipeline` HTTP fallback stays PCM16-only.
- **Firmware Architecture (spool)**: spool records become µ-law at 16 kHz mono (half the byte
  rate); segment metadata gains a `codec` flag ("ulaw16" default for new segments, "pcm16"
  for legacy); replay sends `0xA3` frames with the same capture_ms cadence — elapsed time per
  frame = byte_count samples at 8 kHz-equivalent inference documented so capture deltas remain
  truthful (u-law byte = 1 sample, PCM16 byte = 0.5 sample → capture_ms per frame scales by 2).
- **Notes section**: phrase the phase order — (1) accept both frame types, (2) session opt,
  (3) spool replay; flag Edge Function implementation to `../mynah`.

## 2. `docs/design/ble-audio-bridge.md` §5 note

Add one paragraph under the codec table: row ids are the cross-transport codec ids; the socket
accepts rows 0/10 (0xA1/0xA3) with row-10 halving WiFi TX and doubling spool capacity; row 20
(Opus) stays BLE-reserved.

## 3. Traceability

Frame-type byte choice: `0xA1` (PCM) and `0xA2` were taken by the socket doc; `0xA3` is the
next free type. Consistency clause mirrors the contract (§ Requirement: Consistency with the
BLE bridge).

## Verification

Both docs edited; section cross-check against the EARS list (doc-only spec, no executable
tasks).
