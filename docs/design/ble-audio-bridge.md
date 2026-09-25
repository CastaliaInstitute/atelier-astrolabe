# BLE Audio Bridge (Device ↔ Mynah app)

Status: contract under `goal/ble-audio-bridge` → spec `ble-audio-protocol`. Firmware specs:
`ble-audio-mic-stream` (spec 2), `ble-audio-tts-playback` (spec 3). Mynah-side spec lives for the
`../mynah` repo.

## 1. Overview

BLE is the **primary** voice link between Astrolabe devices and the Mynah Android app. The device
never reaches Castalia directly over WiFi for audio:

```
Astrolabe ──GATT notify──▶ Mynah app ──WSS──▶ Castalia voice-stream (Supabase Edge Function)
Astrolabe ◀─GATT writes─── Mynah app ◀─WSS─── TTS reply audio (PCM/Opus events)
```

- All Castalia credentials live on the phone (Castalia JWT, same model as `VoicePipelineClient.kt`
  in `../mynah`); firmware ships no Supabase URL/anon key for audio.
- WiFi remains enabled ONLY for OTA (`faculty175_ota`) and optional maintenance paths; the audio
  path never falls back to WiFi-direct from the device while BLE is paired.
- Conversational round trip is capped at ≈1.7 s (see §12); behavioral designs shall not add
  buffering beyond the listed jitter buffers.

## 2. Advertisement

Device advertises (existing pattern from `faculty175_ble.c` `ble_build_mfg_payload`):
- Complete local name (e.g. "Astrolabe Faculty").
- Manufacturer data: company id framing (`magic 0xA7`, `version 2`) carrying IMU + observed peer
  hashes — unchanged by this contract.
- ADDITION: the Audio Service UUID (§3) SHALL be included in the advertisement UUID list so a
  phone can filter and connect without service discovery on the first hop.

## 3. GATT Audio Service

Extends the existing Astrolabe UUID base (`faculty175_ble.c:136–150` in `astrolabe175c/main`;
`faculty175_ble.c:44–52` in `astrolabe185b/main`; both files register via `ble_gatts_add_svcs`
at `:2525` / `:552`). Audio service is appended as a second primary service entry in the same
`k_ble_svcs` table before the zero terminator (`astrolabe175c/main/faculty175_ble.c:272` —
table opens with the settings service; `astrolabe185b/main/faculty175_ble.c:75`).

UUID128 base (little-endian init): `41 73 74 72 6f 6c 61 62 65 00 17 50 00 00 00 XX` — i.e.
canonical `XX000000-5017-0000-4173-74726f6c616265` family ("Astrolabe\0\x17P" + trailing byte).

| Trailing byte | Name | Flags | Direction | Notes |
|---|---|---|---|---|
| 0x10 | Audio service | — | — | 128-bit primary service |
| 0x11 | mic audio data | READ + NOTIFY | device → app | CCCD required |
| 0x12 | tts audio data | WRITE_WO_RSP | app → device | no ACK; credit char is the back-pressure channel |
| 0x13 | codec | READ | both | 1 byte, active codec id |
| 0x14 | stream control | WRITE (w/ response) | app → device | binary msg prefix + JSON/body |
| 0x15 | buffer credit | READ + NOTIFY | device → app | u32 LE free bytes in TTS playout ring; CCCD |

Mic and TTS ride the same link concurrently with existing services (battery, settings JSON,
state JSON, health JSON, and the `ASTROLABE_CYBER_FEATURES` control char when present).

## 4. Chunk framing

Every notification / write is a **self-contained, independently decodable chunk**:

```
offset 0   uint16 seq     LE; increments per chunk; wraps 0xFFFE→0 (monotonic field, not per-frame)
offset 2   uint8  codec   0 | 1 | 10 | 11 (Opus = 20, reserved)
offset 3   uint8  frag    0 = whole chunk in this notification (V1 always 0);
                          ≥1 = fragment index for frames larger than one MTU (future Opus)
offset 4   payload        chunk bytes for the codec in §5
```

- Receivers branch on the codec byte per chunk — mid-stream codec changes need no renegotiation.
- Gaps are tolerated (skip forward) in both directions: mic loss degrades STT accuracy
  transiently; TTS loss causes a short audio glitch, never a stall.
- Chunk cadence and payload size per codec are fixed (§5), so the fragment field is redundant for
  regular chunks (always 0) and only exercised by future large-frame codecs.

## 5. Codec matrix + negotiation

| id | format | samples/chunk | chunk bytes | payload/s | firmware picks when |
|---|---|---|---|---|---|
| 0 | PCM16 16 kHz mono LE | 160 (10 ms) | 324 | ≈32.8 kB/s | MTU ≥ 332 (full notification = 4-byte header + 324 ≤ MTU−3 → MTU−7 ≥ 324) |
| 10 | µ-law 16 kHz mono | 160 (10 ms) | 164 | ≈8.4 kB/s | MTU 172–331 |
| 1 | PCM16 8 kHz mono LE | 80 (10 ms @8k) | 164 | ≈8.4 kB/s | unused V1 (10 preferred at equal MTU) |
| 11 | µ-law 8 kHz mono | 80 (10 ms @8k) | 84 | ≈4.2 kB/s | MTU < 172 |

Rule (either direction uses same selection): highest-quality row whose FULL notification
(4-byte header + chunk payload) fits MTU−3, i.e. chunk ≤ MTU−7. Practically: Android phones
negotiate MTU 512 → row 0; iOS caps notification payload (~185) → row 10 (µ-law 16 kHz), which
the app converts to PCM16 before STT. The codec char reflects the active row; a run may change
rows mid-stream (each chunk carries its own codec byte).

## 6. TTS flow control

- Playout ring: 16 kB (≈500 ms of PCM16k; ≈250 ms of PCM16 8k).
- Credit value (char 0x15): `u32 LE free bytes`.
- Notify triggers: every 12.5 % free-space threshold crossing, plus a 500 ms steady-state
  resend while subscribed.
- App protocol: read credit once at start of a TTS run (initial value = full ring), keep a local
  credit, decrement per write-without-response, refresh from notifications. Never exceed credit.
- Overrun is impossible by design; if the app misbehaves, the device overwrite-oldest in the
  ring (audio glitch only, no crash, no link state change).

## 7. Stream control messages (char 0x14)

| msg[0] | body | meaning |
|---|---|---|
| 0x01 | uint32 epoch seconds (LE) | time sync — app writes on connect; device corrects its clock (NTP-free while paired) |
| 0x02 | ASCII JSON | commands: `{"mic":1}` / `{"mic":0}` today; free-form extension point later |

Read on char 0x14 returns state JSON:
`{"arb":"free"|"ring","mic":0|1,"codec":N,"mtu":N,"fs":16000}` — the single source of truth for
the app's session view.

## 8. Single-link arbitration

Astrolabe has one BLE link and already plays central for the Colmi ring
(`faculty175_ble.c` Colmi client state machine). States: `IDLE → STREAMING ⇄ RING`.

- Streaming SHALL begin within 250 ms of (0x11 CCCD subscribed AND 0x14 `{"mic":1}`).
- While `IDLE`, the device runs no mic capture at all.
- Entering RING (Colmi scan/connect begins): stop notify streams, mark `arb:"ring"`; the app is
  expected to stop relaying within one credit tick.
- Exiting: resume streaming fresh (no gap retransmit — live speech, not lossless transfer);
  app re-reads credit before writing TTS.
- While `IDLE`, the device runs no mic capture at all.

## 9. Mynah app contract (repo `../mynah`)

Builds on the existing BLE components in
`android/app/src/main/java/institute/castalia/mynah/ble/` (`AstrolabePresenceCodec.kt` already
decodes the presence advertisement; `MynahBleControlService.kt`/`MynahBleScanner.kt` are the
control-plane seams):

1. **Discover/connect** to the Astrolabe by presence advertisement + Audio Service UUID filter
   (§2), as a second GATT client role alongside the existing control service.
2. **Subscribe** to mic notify (0x11), reassemble by (seq wrap-aware, frag) and forward payload
   bytes to the Castalia `voice-stream` WSS (spec in `docs/design/streaming-audio-socket.md`;
   auth = Castalia JWT on the phone).
3. **Foreground service** is REQUIRED for any sustained streaming session; background execution
   kills sockets and CCCD subscriptions deterministically otherwise.
4. **CCCD liveness watchdog**: if no mic bytes ≤ 4 s after a reconnect while subscription state
   claims active, force exactly one CCCD re-subscribe; on second failure, surface
   `LINK_LOSS_RESUMED`/`LINK_LOST` for UI (see §11 enum).
5. **TTS relay**: transcode reply audio (MP3 from voice-stream, or PCM) to the negotiated codec,
   write chunks honoring §6 credit; include the 4-byte header with codec matching the
   char-0x13 value for the TTS direction.
6. **Reconnect**: re-opening the link resumes streaming; spool backlog pull is future scope
   (recorded in §10/§13 as backlog note), V1 Mynah relays live audio only.

## 10. Firmware contract (specs 2 and 3)

- **Seams**: mic capture reads via `faculty175_audio_read` (`faculty175_board.h:59`, 16 kHz mono
  s16, ES7210, board-ready gate `faculty175_board_audio_ready` at `:49`); TTS playback writes via
  `faculty175_audio_write_pcm` (`faculty175_board.h:65`, ES8311, with the existing
  speaker-mute/PA/volume controls). Both are variant-agnostic.
- **Degradation**: IF a variant lacks a speaker route, THEN only char 0x12 becomes non-functional
  (writes are no-ops); mic streaming, codec char, credit char, and control remain authoritative
  (mic-only mode). The contract itself never branches on variant.
- **Implementation sites**: audio service SHALL be added identically to
  `astrolabe175c/main/faculty175_ble.c` and `astrolabe185b/main/faculty175_ble.c` GATT tables
  (appended entry + CCCD in the same `k_ble_svcs` array).
- **Coexistence**: audio must not starve the control services nor the Colmi ring client: they
  share one link (§8); TX notification budget rule: audio generates at most N-2 notifications per
  conn interval where N is the NimBLE configured TX queue depth, leaving 2 slots for control
  notifies (battery, motion, credit).
- **Budgets**: chunk-packing task ≤ 5 % steady CPU at all codec rows (16 kHz mono worst case);
  task priority above the render loop, below critical control paths; notify-driven chunking, no
  busy-wait.
- **Spool on disconnect**: while the peer is absent mid-turn, capture spools to flash per
  `streaming-audio-socket.md` §Flash spool. V1 firmware: capture-only; spool-pull sync protocol
  over BLE is a follow-up spec.

## 11. Failure enum (surface to Mynah UI)

`BT_OFF` · `PERMISSION` · `RING_BUSY` · `CODEC_MISMATCH` · `CREDIT_OVERRUN` ·
`LINK_LOSS_RESUMED` · `LINK_LOST`

## 12. Latency budget

| leg | cap |
|---|---|
| BLE chunk notify → app | < 30 ms |
| app → Castalia WSS | < 50 ms |
| Castalia STT→LLM→TTS | 300 ms–1.5 s |
| TTS decode + credit + BLE writes | < 80 ms |
| **worst round trip** | **≈ 1.7 s** |

## 13. Traceability

- **Pattern source**: Omi (BasedHardware/omi, Apache-2.0) — app-device protocol reference at
  `docs.omi.me/doc/developer/Protocol`; notify-header + TX-throttle patterns from
  `omi/firmware/omi/src/lib/core/transport.c` (3-byte header → our 4-byte header; reserved-TX-slot
  rule adopted with one slot reserved instead of two due to fewer control chars here). Protocol
  text is ours; only the packet-header idea and throttle rule are borrowed patterns.
- **Repo seams**: `faculty175_board.h:49/59/61/65` (audio ready/read/tdm/write_pcm),
  `faculty175_ble.c` GATT registration `:2525` (175c) / `:552` (185b), existing presence
  advertisement builder `ble_build_mfg_payload`.
- **Endpoint**: Castalia `voice-stream` design and auth contract in
  `docs/design/streaming-audio-socket.md`; this doc supersedes it for AUDIO transport ONLY while
  BLE is the active link; the socket remains the Castalia-side endpoint of the Mynah relay.
