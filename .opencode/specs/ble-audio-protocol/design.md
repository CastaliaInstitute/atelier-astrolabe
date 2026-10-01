# Design: BLE Audio Protocol Contract

Product of this spec: `docs/design/ble-audio-bridge.md`. This design fixes every decision the
document must encode; the doc is a rendering of it plus repo-traceable anchors.

## UUID derivation

Existing base in both variants' `faculty175_ble.c` GATT tables:

```
BLE_UUID128_INIT(0x41,0x73,0x74,0x72,0x6f,0x6c,0x61,0x62,0x65,0x00,0x17,0x50,0x00,0x00,0x00,0x01)
```

Little-endian init of UUID128 whose canonical form is
`01000000-5017-0000-6562-616c6f72747341`. Service bytes end `…0x01`; used chars end `0x02..0x06`.
Audio service extends the same prefix:

| Element | Last byte (LE init) | Notes |
|---|---|---|
| Audio service | `0x10` | primary service |
| mic audio data | `0x11` | READ + NOTIFY; CCCD present |
| tts audio data | `0x12` | WRITE_WO_RSP (no ACK; credit char is the flow signal) |
| codec | `0x13` | READ, 1 byte |
| stream control | `0x14` | WRITE w/ response (reliability for commands) |
| buffer credit | `0x15` | READ + NOTIFY; CCCD |

## Chunk framing

```
offset 0   uint16 seq       little-endian, increments per chunk, wraps 0x10000 -> 0
offset 2   uint8  codec     0/1/10/11 (Opus 20 reserved)
offset 3   uint8  frag      0 when single notification (V1 always 0); ≥1 fragment list
offset 4   payload
```

- V1 sends whole chunks per notification: payload ≤ MTU−3−4 after header. Fragment bytes reserved
  for future Opus frames, which are larger than one MTU.
- Mic: fixed cadence per codec (below). TTS: variable payload, app-paced by credit.
- Receivers branch on codec byte per chunk — decoder switches are stateless.

## Codec / chunking / negotiation table

| codec | format | samples/chunk | chunk bytes | payload sent/s | pick when |
|---|---|---|---|---|---|
| 0 | PCM16 16k mono | 80 (5 ms) | 164 | ~16.4 kB/s | MTU ≥ 247 |
| 10 | µ-law 16k mono | 160 (10 ms) | 164 | ~8.4 kB/s | MTU 167–246 |
| 1 | PCM16 8k mono | 80 (10 ms) | 164 | ~8.4 kB/s | unused V1 (10 preferred) |
| 11 | µ-law 8k mono | 80 (10 ms) | 84 | ~4.2 kB/s | MTU < 167 |

Negotiation rule (firmware, after MTU exchange): pick the highest row whose chunk fits
MTU−3, write that byte to the codec char. STT-quality note documented: PCM16 16k is the
normal path; µ-law rows are the small-MTU fallback (phones rarely below 185).

## Flow control (TTS direction)

- Playout ring: 16 kB (~500 ms of PCM16k). Device notifies available-butewaitcredit =
  `u32 LE free_bytes` on: 12.5 % free-space threshold crossings while subscribed, and every
  500 ms while subscribed. App keeps a local credit, decrements it on each write-no-rsp, and
  refreshes from notify. Start credit (right after first read) = full ring size.
- Device-side overflow is impossible by construction; app over-run (bug/loss) degrades to
  overwrite-oldest in ring (documented, no crash, audio glitch only).

## Stream control messages (char 0x14)

- `0x01 + u32 LE epoch_s` — time sync, written by app on connect.
- `0x02 + ASCII JSON` — commands: `{"mic":1}` /`{"mic":0}`; future `{"face":...}` free-form
  extension point. Response (read on 0x14): JSON `{"arb":"free"|"ring","mic":0|1,"codec":N,"mtu":N}`.

## Arbitration state machine (single BLE link)

`IDLE → STREAMING (app subscribed+mic on) ⇄ RING (forced when Colmi scan/connect begins)`
- Entry into RING: stop notifies, wait broadcasts `arb:"ring"`.
- Exit: resume stream (fresh credit re-read), do not re-xmit gap chunks (live STT, not lossless).
- Mic/duty: while IDLE, no audio read at all.

## Offline / spool interplay

IF the app disconnects mid-turn, THEN the firmware continues capturing to the flash spool defined
by `docs/design/streaming-audio-socket.md` §Flash spool; on reconnect, the app MAY pull spool
first (out of V1 firmware scope — recorded as a firmware spec 2 backlog note in the doc).

## Latency budget (documented cap)

BLE chunk→app < 30 ms · app→socket < 50 ms · Castalia STT/LLM/TTS 300 ms–1.5 s · TTS decode+credit+
BLE-write < 80 ms ⇒ worst conversational round trip ≈ 1.7 s. Contract states this cap and forbids
adding buffering above the listed jitter buffers spuriously.

## Supervision / failure enum (for app UI)

`BT_OFF, PERMISSION, RING_BUSY, CODEC_MISMATCH, CREDIT_OVERRUN, LINK_LOSS_RESUMED, LINK_LOST`.

## Section order in the produced document

1. Overview (BLE primary decision, WiFi=OTA only) 2. Advertisement 3. GATT table 4. Framing
5. Codecs 6. Flow control 7. Control messages 8. Arbitration 9. Mynah app contract 10. Firmware
contract 11. Failure enum 12. Latency budget 13. Traceability (Omi refs, repo anchors, socket doc).
