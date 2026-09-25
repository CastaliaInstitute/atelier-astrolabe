# Streaming Audio Socket

Astrolabe devices should move from single HTTP PCM uploads to a bidirectional
audio event socket. Flash remains in the path, but as a circular spool and
replay buffer instead of a single utterance file.

## Goals

- Stream microphone PCM while speech is still happening.
- Avoid one large RAM buffer and avoid one overwrite-prone SPIFFS file.
- Let the backend send transcript, reply, and audio playback events as soon as
  they are available.
- Keep a flash circular buffer so brief network stalls do not lose audio.
- Preserve the existing `voice-pipeline` HTTP endpoint as a fallback.

## Reference Pattern

OpenAI Realtime uses a WebSocket event stream where clients configure a session,
append audio chunks to an input buffer, commit the buffer, and receive ordered
server events such as speech start/stop, transcript, audio deltas, and response
completion. Astrolabe should use the same shape, but prefer binary PCM frames
from firmware to avoid base64 overhead on small devices.

Supabase Edge Functions support WebSocket upgrades, so the Castalia endpoint can
be hosted as an Edge Function and can later relay to OpenAI Realtime, Google
streaming STT, or the existing `voice-pipeline` route.

## Endpoint

```text
wss://<project>.supabase.co/functions/v1/voice-stream
```

Authentication matches `voice-pipeline`:

- `apikey: <Supabase anon key>`
- `Authorization: Bearer <Castalia JWT or anon key>`
- existing Astrolabe device HMAC headers when required

## Client Events

JSON control messages are UTF-8 text WebSocket frames:

```json
{"type":"session.update","session":{"audio":{"input":{"format":"pcm16","sampleRateHz":16000,"channels":1}},"face":"faculty","facultySlug":"a.einstein","facultyName":"Einstein"}}
```

Session routing modes:

```json
{"type":"session.update","session":{"interactionMode":"conversation","commonplaceMode":"conversation"}}
```

```json
{"type":"session.update","session":{"interactionMode":"journal","commonplaceMode":"journal","logToCommonplace":true}}
```

- `interactionMode: "conversation"` runs the normal STT -> LLM -> optional TTS path.
- `interactionMode: "transcribe"` runs STT only and returns no immediate reply. It
  logs only when `commonplaceMode` / `logToCommonplace` asks for it.
- `interactionMode: "journal"` runs STT only, returns no immediate reply, and
  defaults to writing the transcript as a Commonplace journal entry.
- `commonplaceMode: "off" | "conversation" | "journal"` controls the Castalia
  Commonplace write. Boolean `logToCommonplace` is accepted as shorthand.

```json
{"type":"input_audio_buffer.commit","turnId":"turn-42"}
```

```json
{"type":"input_audio_buffer.clear"}
```

```json
{"type":"response.cancel"}
```

Audio messages should be binary frames:

```text
0xA1 <uint32 little-endian sequence> <uint32 little-endian capture_ms> <pcm16 bytes>
```

The server may also accept OpenAI-style JSON append messages for browser tools
and debugging:

```json
{"type":"input_audio_buffer.append","audio":"<base64 pcm16>"}
```

### µ-law frame type (0xA3)

Clients MAY send µ-law 16 kHz mono payloads as `0xA3` frames with the same header:

```text
0xA3 <uint32 little-endian sequence> <uint32 little-endian capture_ms> <u-law bytes>
```

Rules:

- The endpoint normalizes every frame to PCM16 16 kHz mono before buffering/commit
  (µ-law decode is an exact 256-entry inverse table; no STT behavior differences).
- An odd µ-law byte count is tolerated: the endpoint drops the trailing byte and counts it.
- `0xA1` remains the default. `session.update` opts into µ-law, echoing the BLE bridge row ids
  (`docs/design/ble-audio-bridge.md` §5): `{"audio":{"input":{"format":"ulaw16"}}}`.
- The `voice-pipeline` HTTP fallback stays PCM16-only.

## Server Events

Server messages are JSON text frames unless the type explicitly carries binary
audio.

```json
{"type":"session.created","sessionId":"..."}
```

```json
{"type":"input_audio_buffer.speech_started","turnId":"turn-42"}
```

```json
{"type":"input_audio_buffer.speech_stopped","turnId":"turn-42"}
```

```json
{"type":"input_audio_buffer.committed","turnId":"turn-42","pcmBytes":312320}
```

```json
{"type":"conversation.item.input_audio_transcription.completed","turnId":"turn-42","transcript":"..."}
```

```json
{"type":"response.text.delta","turnId":"turn-42","delta":"..."}
```

```json
{"type":"response.audio.delta","turnId":"turn-42","audio":"<base64 mp3 or pcm chunk>"}
```

```json
{"type":"response.done","turnId":"turn-42"}
```

```json
{"type":"error","code":"stt_timeout","message":"No transcript before timeout"}
```

## Firmware Architecture

The capture task writes every frame into a flash ring before or while it sends
that frame over WebSocket.

```text
mic -> VAD -> flash ring segment N -> websocket tx queue
             flash ring segment N+1
             flash ring segment N+2
```

Recommended ring:

- 4 to 8 segment files under `/spiffs`.
- Segment size 2 to 5 seconds of PCM.
- Metadata per segment: sequence, start_ms, byte_count, committed flag.
- Writer never overwrites a segment that is still queued for network send.
- If the network is down and the ring fills, drop oldest uncommitted audio and
  emit a local QA/error counter.

### Spool codec (µ-law default)

New spool segments SHALL store µ-law 16 kHz mono records (half the byte rate of PCM16,
doubling effective spool capacity) and carry a `codec` metadata flag: `"ulaw16"` for new
segments, `"pcm16"` for legacy segments (rollout keeps both readable). Replay sends `0xA3`
frames; `capture_ms` stays truthful because one µ-law byte is one sample versus one PCM16
byte being half a sample — the per-frame elapsed time doubles for the same byte count
(16 kB segment = ~34 s of µ-law vs ~17 s of PCM16).

The WebSocket task drains queued PCM frames or segments. If the socket is open,
it sends live binary frames. If the socket is closed, it reconnects and replays
available unsent ring segments in order.

## Backend Phases

1. `voice-stream` accepts WebSocket connections and buffers received PCM until
   `input_audio_buffer.commit`.
2. On commit, call the existing `voice-pipeline` logic so firmware can switch
   transports without changing Castalia behavior.
3. Replace buffered commit with streaming STT when the selected provider supports
   it.
4. Stream TTS response audio chunks back over the same socket.
5. Add resumable session IDs and event replay for device reconnects.

## Notes

- This protocol intentionally resembles OpenAI Realtime events but is not a
  direct copy. It keeps device-friendly binary PCM and Castalia faculty routing.
- The current HTTP `voice-pipeline` endpoint should remain as a fallback and QA
  tool.
- A socket does not remove the need for flash. Flash is the resilience layer when
  Wi-Fi stalls or the edge worker restarts.
- Endpoint rollout order for µ-law support: (1) accept both `0xA1`/`0xA3` frame
  types, (2) `session.update` format opt-in, (3) spool replay. Edge Function
  implementation lives in the mynah repo (`../mynah`, `supabase/functions`);
  codec row ids mirror `docs/design/ble-audio-bridge.md` §5 (0 = PCM16, 10 =
  µ-law 16 kHz, 20 = Opus, BLE-reserved).

## Supersession: BLE audio bridge (2026-09-25)

When the device is paired over BLE to the Mynah app (`docs/design/ble-audio-bridge.md`), the
audio transport path is device → BLE → Mynah app → this `voice-stream` socket. The device never
opens this socket itself on the BLE path: the Mynah app becomes the socket client (Castalia JWT
on the phone, same auth model as `VoicePipelineClient.kt` in `../mynah`) and the GATT client of
the device's Audio Service. The socket design below remains the Castalia-side endpoint and
event protocol (session events, binary PCM frames, commit/turn ids). Flash spooling stays in
the device for link-loss resilience; the spool-pull BLE sync protocol is a follow-up spec.
This document's device-direct WSS leg, `voice-pipeline` HTTP fallback, and OTA via
`faculty175_ota` remain as maintenance/QA transports only.
