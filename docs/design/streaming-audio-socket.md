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
