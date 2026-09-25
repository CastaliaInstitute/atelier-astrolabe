# Tasks: BLE Audio Mic Stream

- [x] 2.1 Create `faculty175_ble_audio.h/.c` with the Audio GATT service definition and access callbacks (chars 0x11–0x15, TTS stub, credit read) and add the service into both variants' `k_ble_svcs` tables (mic-stream: service registration; compatibility)
- [x] 2.2 Implement capture/pack loop: `blea_mic_task`, codec rows 0/10/11 with µ-law LUT, chunk header/seq wrap, counters, mbuf notify path with drop-not-block (mic streaming; task budgets)
- [x] 2.3 Implement TX credit semaphore + NOTIFY_TX/disconnect gap-event routing + msys reset hook (TX budget)
- [x] 2.4 Implement stream-control char handlers (0x01 epoch time sync, 0x02 mic JSON, read state JSON incl. arb) (mic streaming; arbitration integration)
- [x] 2.5 Wire init/MTU-negotiation/Colmi toggle hooks and advertisement UUID into both variants' `faculty175_ble.c` (codec negotiation; compatibility; arbitration integration)
- [x] 2.6 Spool-on-loss route hook (capture-only) (spool on loss)
- [x] 2.7 Serial QA hook `qa blemic` counters in both variants (verification)
- [ ] 2.8 Verify: shasum byte-identical copies; build both variants; record results; operator device QA plan (verification)
