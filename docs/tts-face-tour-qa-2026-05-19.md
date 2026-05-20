# TTS Face Tour QA - 2026-05-19

Device: Waveshare ESP32-S3 1.75C / Mynah Astrolabe  
Firmware under test: dirty build from `fb5b8f9` plus local playback, gesture, BLE, voice, and heap fixes  
Port used during run: `/dev/cu.usbmodem1401`  
Network identity during passing run: `astrolabe.local`, `192.168.86.182`

## Summary

The comprehensive `qa tour tts 1200` run completed all 19 faces and reached `tour: done`.
Each face loaded and the TTS button-test path produced an MP3 response in the passing run.

The tour also exposed the current weak point: internal heap fragmentation and TLS pressure.
Fresh boot was healthy (`heap=141204 largest=131060 psram=7732711`), but face-by-face TTS settled near
`73 KB` internal free with a `55 KB` largest block. A second warm run, started immediately after a prior
tour without rebooting, degraded to `heap=25912 largest=9204`, wedged the web screenshot endpoint, and
caused repeated TLS failures.

Raw MP3 TTS currently reports MP3 status and size over serial, but does not expose the generated spoken
text. Runtime task stack high-water metrics are not exposed by firmware yet; configured task stack sizes
are listed below.

## Passing TTS Tour

Source log: `artifacts/monitor/tts-tour-final-3.log`

| # | Face | Heap | Largest | PSRAM | TTS response | POST body | Notes |
|---:|---|---:|---:|---:|---:|---:|---|
| 0 | classic | 142172 | 131060 | 7732983 | MP3 212160 B | 440 B | I2S uninstall warning |
| 1 | apocalypso | 108668 | 55284 | 7732711 | MP3 292032 B | 457 B | I2S uninstall warning |
| 2 | digital | 108244 | 55284 | 7732711 | MP3 66048 B | 448 B | I2S uninstall warning |
| 3 | spotify | 108020 | 55284 | 7732711 | MP3 110016 B | 441 B | I2S uninstall warning |
| 4 | astro | 107796 | 55284 | 7732711 | MP3 38784 B | 1072 B | I2S uninstall warning |
| 5 | moon | 107488 | 55284 | 7732711 | MP3 161472 B | 834 B | I2S uninstall warning |
| 6 | calcifer | 107264 | 55284 | 7732711 | MP3 102720 B | 440 B | I2S uninstall warning |
| 7 | castalia | 107264 | 55284 | 7732711 | MP3 158592 B | 427 B | I2S uninstall warning |
| 8 | settings | 73400 | 55284 | 7732711 | MP3 122112 B | 432 B | I2S uninstall warning |
| 9 | synastry | 73624 | 55284 | 7732711 | MP3 38400 B | 503 B | I2C burst before Spectrum, I2S uninstall warning |
| 10 | spectrum | 73624 | 55284 | 7732711 | MP3 82176 B | 421 B | I2S uninstall warning |
| 11 | chakra | 73624 | 55284 | 7732711 | MP3 153600 B | 413 B | I2S uninstall warning |
| 12 | bowl | 73624 | 55284 | 7732711 | MP3 54336 B | 386 B | I2S uninstall warning |
| 13 | rocket | 73624 | 55284 | 7732711 | MP3 77184 B | 389 B | I2S uninstall warning |
| 14 | radar | 73624 | 55284 | 7732711 | MP3 73152 B | 399 B | BLE paused for TTS heap, I2S uninstall warning |
| 15 | faculty | 73304 | 55284 | 7732711 | MP3 44544 B | 423 B | I2S uninstall warning |
| 16 | weather | 73304 | 55284 | 7732711 | MP3 49344 B | 372 B | I2S uninstall warning |
| 17 | quotes | 73304 | 55284 | 7732711 | MP3 170880 B | 377 B | I2S uninstall warning |
| 18 | transits | 73528 | 55284 | 7732711 | MP3 262848 B | 456 B | I2S uninstall warning |

Final result: `TTS_TOUR PASS`, all faces `[0..18]` seen, `errors=0` in the harness.

## Warm Low-Heap Stress Tour

Source artifacts: `artifacts/qa/tts-tour-20260519-2138/summary.json` and
`artifacts/qa/tts-tour-20260519-2138/serial-timestamped.log`

This run was started immediately after a prior TTS tour, without a clean reboot. It is useful because it
shows what happens after the device is already fragmented and the HTTP screen server is under pressure.

| # | Face | Heap | Largest | TTS result | Screenshot capture | Key finding |
|---:|---|---:|---:|---|---|---|
| 0 | classic | 73760 | 57332 | failed | timed out | HTTP `-5`, retry failed |
| 1 | apocalypso | 63636 | 32756 | failed | timed out | TLS connect failed; SHA buffer allocation failed |
| 2 | digital | 63636 | 32756 | timeout | timed out | Voice request hung until tour timeout |
| 3 | spotify | 25912 | 9204 | skipped | timed out | Worst heap sample; follow-on `voice busy` |
| 4 | astro | 62336 | 34804 | skipped | host down | `voice busy` carryover |
| 5 | moon | 61544 | 34804 | skipped | host down | `voice busy` carryover |
| 6 | calcifer | 61544 | 34804 | skipped | host down | `voice busy` carryover |
| 7 | castalia | 61544 | 34804 | skipped | host down | TLS failed while old request unwound |
| 8 | settings | 67148 | 38900 | failed | host down | TLS retry failed |
| 9 | synastry | 67148 | 38900 | failed | host down | TLS retry failed; I2C errors followed |
| 10 | spectrum | 67148 | 38900 | failed | timed out | TLS retry failed |
| 11 | chakra | 67148 | 38900 | failed | no route | TLS retry failed |
| 12 | bowl | 67148 | 38900 | failed | host down | TLS retry failed |
| 13 | rocket | 67148 | 38900 | failed | timed out | TLS retry failed |
| 14 | radar | 67148 | 38900 | failed | no route | BLE paused correctly; TLS retry failed |
| 15 | faculty | 67148 | 38900 | failed | host down | TLS retry failed |
| 16 | weather | 67148 | 38900 | failed | timed out | TLS retry failed |
| 17 | quotes | 67148 | 38900 | failed | no route | Faculty bust skipped low memory |
| 18 | transits | 67148 | 38900 | failed | host down | TLS retry failed |

Final result: tour still reached `tour: done`, but TTS was not functional after the early heap/TLS collapse.

## Screenshots

Screenshot capture was attempted from `GET /screen.bmp` during the instrumented warm run. Every capture
failed because the HTTP server timed out or became unreachable while TTS/TLS was under low-heap pressure.
After a watchdog reset, serial showed the app alive, but USB later disappeared and HTTP remained unreachable
from this machine, so a clean screenshot pass could not be completed in this session.

This should become a hard QA signal: a TTS tour report is incomplete unless `/screen.bmp` remains reachable.
The next instrumentation pass should capture screenshots in a separate clean no-TTS face pass, or add a
serial/JTAG framebuffer dump path so screenshots do not depend on the web server during TLS pressure.

## Stack Metrics

Runtime stack high-water marks are not currently printed by the firmware. Configured stack sizes found in
the build:

| Task | Configured stack |
|---|---:|
| `voice_net` | 32768 B |
| `spk_play` | 32768 B |
| `faculty_bust` | 32768 B |
| `commonplace_net` | 32768 B |
| `castalia_net` | 32768 B |
| `ble_init` | 10240 B |
| `uac_tusb` | 4096 B |

Recommended instrumentation: add `uxTaskGetStackHighWaterMark()` reporting for `voice_net`, `spk_play`,
`loopTask`, `faculty_bust`, and `ble_init`, and include it in `qa heap` or a new `qa tasks` command.

## Bugs Confirmed By Tour

- Raw MP3 TTS playback works in a fresh/passing tour, but it is highly sensitive to internal heap pressure.
- A warm second tour can wedge TTS into `voice busy` after a timeout.
- The web screenshot/log server can become unreachable during low-heap TTS pressure.
- Faculty bust loading is still skipped under low heap.
- Repeated `i2s_driver_uninstall(...): I2S port 0 has not installed` warnings are noisy and should be
  made idempotent.
- Spectrum transition can trigger an I2C `requestFrom()` error burst.

## Follow-Up Work

1. Add runtime stack high-water QA output.
2. Add serial/JTAG screenshot capture or make web screenshot capture resilient during TTS.
3. Ensure failed voice requests always clear the voice busy state before the tour advances.
4. Start HTTP after late Wi-Fi reconnects, not only during setup.
5. Make I2S stop/uninstall idempotent to remove false-positive error noise.
6. Continue moving TTS buffers and faculty bust work out of internal heap.
