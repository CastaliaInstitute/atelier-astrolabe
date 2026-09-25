# Tasks: BLE Audio TTS Playback

- [x] 3.1 Ingestion: TTS write handler parses header/codec, fills the 16 kB playout ring, overwrite-oldest alignment, seq-gap + badcodec counters (TTS write ingestion; codec mismatch)
- [x] 3.2 Playout task: priming gate, per-chunk decode (µ-law inverse LUT, 8k×2 upsample), writes via `faculty175_audio_write_pcm` with skip-underflow, route-not-ready drop counting (playout; compatibility and budgets)
- [x] 3.3 Credit notify path: 12.5 % thresholds + 500 ms subscribe-gated ticks from the playout task, disconnect stop (credit notifications)
- [ ] 3.4 Extend `facility/lib175_ble_audio_qa()` TTS counters; re-cp module to both variants, shasum-verify, build 175c + 185b (verification)
