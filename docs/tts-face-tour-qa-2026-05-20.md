# TTS Face Tour QA - 2026-05-20

Hardware run on the bench watch after flashing the current `integration` worktree.

## Result

**Failing, no crash.** The tour visited all 19 faces and reached `tour: done`, but the TTS gate failed:

```text
tour: summary tts_ok=7 tts_fail=7 tts_skip=5
TTS_TOUR FAIL
```

Source artifacts:

- Summary: [`artifacts/qa/tts-tour-20260520-041016/summary.json`](../artifacts/qa/tts-tour-20260520-041016/summary.json)
- Serial log: [`artifacts/qa/tts-tour-20260520-041016/serial-timestamped.log`](../artifacts/qa/tts-tour-20260520-041016/serial-timestamped.log)
- Capture harness: [`scripts/tts_tour_capture.py`](../scripts/tts_tour_capture.py)

## Command

```bash
ASTROLABE_FLASH_SMOKE_SEC=20 ./scripts/ci-flash.sh
mcp/astrolabe-esp/.venv/bin/python scripts/tts_tour_capture.py --dwell-ms 1200
```

The firmware flash completed successfully. The PlatformIO smoke monitor failed only because the noninteractive session could not use terminal APIs; the upload itself completed and reset the app.

## Observations

| Metric | Value |
|---|---:|
| Faces seen | 19 / 19 |
| TTS playback OK | 7 |
| TTS failures | 7 |
| TTS skips | 5 |
| Screenshot captures OK | 8 / 19 |
| Crash / panic count | 0 |

The first seven faces played TTS successfully: Classic, Apocalypso, Digital, Spotify, Astro, Moon, and Calcifer.

Castalia then hit TLS allocation failure:

```text
[ssl_client.cpp:37] _handle_error(): [start_ssl_client():264]: (-32512) SSL - Memory allocation failed
voice: message fail (HTTP -1)
tour: narrate failed 7 err=HTTP -1
```

Settings started a TTS request and timed out. After that, `voice busy` caused Synastry through Rocket to skip, and Radar through Transits failed repeated HTTPS attempts with `start_ssl_client: -1`.

The screen endpoint degraded at the same point: screenshots succeeded through Castalia, Settings timed out, later captures timed out or reported host down.

## Captured Faces

These screenshots were captured successfully before the screen endpoint degraded:

- [Classic](../artifacts/qa/tts-tour-20260520-041016/00-classic.bmp)
- [Apocalypso](../artifacts/qa/tts-tour-20260520-041016/01-apocalypso.bmp)
- [Digital](../artifacts/qa/tts-tour-20260520-041016/02-digital.bmp)
- [Spotify](../artifacts/qa/tts-tour-20260520-041016/03-spotify.bmp)
- [Astro](../artifacts/qa/tts-tour-20260520-041016/04-astro.bmp)
- [Moon](../artifacts/qa/tts-tour-20260520-041016/05-moon.bmp)
- [Calcifer](../artifacts/qa/tts-tour-20260520-041016/06-calcifer.bmp)
- [Castalia](../artifacts/qa/tts-tour-20260520-041016/07-castalia.bmp)

## Follow-Up Tasks

1. Make `pm_voice` clear the busy state reliably after HTTPS timeout and retry exhaustion.
2. Add a pre-TTS heap gate using largest internal block, not just total heap, so the tour skips before TLS wedges.
3. Defer or suppress Castalia pairing/network warmup during TTS tour to protect TLS heap.
4. Move screenshot capture off the same HTTP surface or add a recovery path when `/screen.bmp` stalls during TTS pressure.
5. Reduce per-face TTS response size or stream/decode in smaller chunks so repeated MP3 downloads do not fragment internal heap.
