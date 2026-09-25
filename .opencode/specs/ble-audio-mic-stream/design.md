# Design: BLE Audio Mic Stream (firmware)

## Module layout

New pair `faculty175_ble_audio.h` / `faculty175_ble_audio.c`, delivered **byte-identically** to
both `astrolabe175c/main/` and `astrolabe185b/main/` (repo convention: variant trees duplicate
shared files — matches the divergent `faculty175_ble.c` copies). `k_ble_svcs` in each variant's
`faculty175_ble.c` gains the audio service entry plus an include/init call; everything else in
ble.c stays untouched.

## GATT table entry (per contract §3)

- Audio service UUID: `BLE_UUID128_INIT(0x41,0x73,0x74,0x72,0x6f,0x6c,0x61,0x62,0x65,0x00,0x17,0x50,0x00,0x00,0x00,0x10)`,
  chars with trailing bytes `0x11..0x15` in the same order as the contract table.
- Access callbacks: `blea_chr_codec_read` (1 byte from state), `blea_chr_control_access`
  (write 0x01 epoch / 0x02 JSON, read returns state JSON), `blea_chr_tts_write` (stub: return len
  = consumed, drop payload V1), `blea_chr_credit_read` (u32 LE free bytes of the TTS ring —
  returns full-ring while spec 3 is pending).
- CCCD definitions for 0x11 and 0x15 (`BLE_GATT_CHR_F_NOTIFY` + `ble_cccd` attr).

## Streaming state machine (in ble_audio.c)

```
IDLE --subscribe0x11 + {"mic":1}--> RUN --unsub or {"mic":0}--> IDLE
RUN  --colmi_active()>0 Stateless --> PAUSED --colmi quiesce --> RUN
RUN  --gap disconnects--> IDLE (spool route stays on until turn close)
```

State kept in one static struct guarded by `portMUX`; no malloc on the streaming path.

## Capture/pack loop

- Task `blea_mic_task` (priority `FACULTY175_AUDIO_TASK_PRIO` = one step above render loop;
  stack 3 kB internal RAM), created at NimBLE sync and immediately parked.
- Cadence: `faculty175_audio_read(pcm80, 80, &got, 50ms)` per codec-row cadence; group-level
  reads slide (row 0 = 80/5 ms; µ-law rows convert 160-sample blocks).
- Encoding: row 0 passthrough bytes; rows 10/11 linear→µ-law LUT (256-entry const table).
- Chunking: header built per §4; seq wraps; counter stats (chunks_sent, dropped_notify, seq_wrap)
  for the `qa blemic` printer.
- Notify: pre-allocated mbuf via `ble_msys_pool_get`-equivalent path → `os_mbuf_append` header+
  chunk → `ble_gatts_notify_custom(conn, chr_val_handle, mbuf)`; if mbuf alloc fails, count
  `dropped_notify` and skip (never block the audio thread on the host).
- Gate: before each commit check `is_subscribed(0x11)` + state==RUN + board audio ready.

## TX budget

Audio credit semaphore (`blea_tx_sem`, count = audio mbuf pool size − 2) decremented before
`notify_custom`, released in the TX-done callback path via semaphore give (mirrors Omi's
`AUDIO_TX_RESERVED_SLOTS` pattern); on gap disconnect, semaphore re-init (ble.c already calls the
audio module's reset hook).

## Negotiation hook

ble.c connection-open path calls `faculty175_ble_audio_on_mtu(conn, mtu)` after MTU exchange:
selects row per contract §5, persists into state, updates codec char value source. Default when
unknown: row 10 (safe at MTU 167).

## Integration edits (both variants' faculty175_ble.c)

1. `#include "faculty175_ble_audio.h"` next to existing includes; audio service appended into
   `k_ble_svcs` before terminator via `faculty175_ble_audio_svc_def()` accessor returning the
   `ble_gatt_svc_def`.
2. `nimble_host` init after `ble_gatts_add_svcs`: `faculty175_ble_audio_start()` (spawn task).
3. Gap event `BLE_GAP_EVENT_NOTIFY_TX`/disconnects routed to `faculty175_ble_audio_gap(ev)`.
4. Colmi scan/connect entry/exit call `faculty175_ble_audio_set_ring(bool)`.
5. Advertisement: append audio service UUID to `ble_build_adv_fields` uuids128 list.
6. Serial QA: hook `qa blemic` string into the existing `qa` command table (prints counters).

## Spool hook

On `RUN` and `gap-disconnect` with turn active: mic loop routes chunk payloads (PCM16 16k,
pre-µ-law) into `faculty175_flash_ring_write()` accessor (spool defined in
streaming-audio-socket.md §Firmware Architecture; spool module already staged for voice path —
if absent at implementation time, this task is limited to a clearly-labeled no-op hook).

## Verification

- `shasum` both copies equal.
- Both-variant build.
- `qa blemic`: counters live-view; operator device test enumerates: connect phone → `{"mic":1}`
  stream observed via nRF Connect / app; pause/resume on ring events; disconnect spool path
  (log line).
