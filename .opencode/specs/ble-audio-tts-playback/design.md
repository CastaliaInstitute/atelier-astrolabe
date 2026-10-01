# Design: BLE Audio TTS Playback (firmware)

All changes live in `faculty175_ble_audio.c/.h` (both variant copies stay byte-identical);
`faculty175_ble.c` is NOT edited further (0x12 access cb + 0x15 char already registered; only
the module internals change).

## Playout ring

```c
static uint8_t s_tts_ring[BLEA_TTS_RING_BYTES];  /* 16 kB, extends current XRAM use */
static size_t  s_tts_head, s_tts_tail;           /* copy ring, head=write pos, tail=read pos */
static size_t  s_tts_free_bytes;                 /* shared with credit path */
```

- Producer: `blea_chr_tts_access` write handler (NimBLE host task), single writer.
- Consumer: new `blea_tts_task` (stack 4 kB internal, priority = BLEA_MIC_TASK_PRIO).
- Guarding: `portMUX` on head/tail/free; payload copies ≤ 244 B; no heap allocation.
- Overwrite-oldest on overflow: advance tail past the oldest FULL chunk boundary (header
  alignment: boundary scan by walking chunk lengths), count `tts_dropped`.

## Ingestion (write handler)

1. `OS_MBUF_PKTLEN` < 4 → count `tts_badchunks` and return 0 (consume, contract: never stall).
2. Pull flat buffer 4 .. MTU−3; verify codec ∈ {0,10,11} else count `tts_badcodec` and discard.
3. Track `s_tts_last_seq` for gap counting (QA only, no retransmit).
4. If payload > free: drop OLDEST chunks until payload fits (or payload > ring → discard all).
5. memcpy payload into ring at head, advance head, update free.

## Playout task loop

```text
wait until run gate (board ready && (mic_on considers? no — TTS independent))
primed = false
loop every 20 ms:
   if free_bytes == ring and primed: stay idle
   if !primed and ring >= 320B: primed = true
   if primed: pop up to 160 samples worth; decode; faculty175_audio_write_pcm(..., 100ms)
              on failure (route not ready): count tts_underflow, drop popped, primed=false
   credit tick (below)
```

- Decode: LUT `u16 s_ulaw_decode[256]` built once at start (inverse companding math), or reuse
  the telephony formula inverted; codec 0 passthrough; codec 11 decimated 8k→16k by repetition?
  NO — `faculty175_audio_write_pcm` takes 16k mono; codec 11 (8k) is upsampled 2× by nearest
  duplicate before write (driver rate stays fixed 16k).
- Mic/speaker arbitration: rely on the board layer's existing route management exactly like the
  voice MP3 path does (`faculty175_audio_reset_speaker` is called by consumers; write_pcm handles
  open). Keep playout task from calling mute/volume (contract says app/state controls own those).

## Credit notifies

- `blea_credit_notify(conn, force)`: compute free bytes, build u32 LE, `ble_gatts_notify_custom`
  on `s_chr_credit_handle`; failures counted, never retry.
- Triggers inside playout task: compute thresholds at ring/8 boundaries vs `s_credit_last_level`;
  plus a 500 ms timer while `s_credit_subscribed` (set/cleared by gap SUBSCRIBE handler on the
  0x15 handle; disconnected → stop).
- `V1`: credit char value read path already returns ring size; now returns true free bytes.

## QA

Extend `faculty175_ble_audio_qa()` output with TTS line: `blemic: tts_written=%lu dropped=%lu
underflow=%lu credit=%lu`. One hook (`qa blemic`) only; no new qa command.

## Verification

`shasum` both module copies; 175c + 185b builds; operator QA: phone-initiated TTS run while
watching `qa blemic` counters (written == app writes, credit drains/refills ≈ 16 kB envelope).
