# ESP-NOW Psychometer / Wellness Receiver

Small ESP-IDF receiver used to verify Elecrow128 psychometer broadcasts and
interim family wellness summaries from ring-backed Astrolabes.

It listens on ESP-NOW channel `6` and logs:

- `PSYC` (`0x50535943`) psychometer packets from Elecrow firmware.
- `WELL` (`0x57454c4c`) family wellness summary packets from Astrolabe ring
  bridges.

Build and flash to a second ESP32-S3:

```bash
source "$IDF_PATH/export.sh"
./scripts/espnow_psych_rx_build.sh build
./scripts/espnow_psych_rx_build.sh -p /dev/ttyACM0 flash monitor
```

Expected serial output:

```text
I psych_rx: psych packet mac=14:c1:9f:26:74:7c seq=42 arousal=54 valence=50 focus=arousal enc=2 press=0 age=12345
I psych_rx: wellness packet mac=31:32:42:31:c9:03 subject=1 seq=43 flags=0x3f stress=72 trend30m=9 hrv=38 hr=91 spo2=97 sleep=338 light=182 deep=74 rem=52 awake=30 batt=81 age=23456
I psych_rx: tts_context subject=Camille status=fresh age_ms=0 cue=check-in-soon stress=72 trend30m=9 hrv=38 hr=91 spo2=97 sleep=338 sleep_debt=82 batt=81 flags=0x3f
I psych_rx: paired_state subject=Camille status=fresh age_ms=12000 seq=43 source=31:32:42:31:c9:03
```

`WELL` packets are summaries, not raw ring packets. The intended publisher is an
opted-in Astrolabe that has already converted Colmi R10 data into HRV, stress,
sleep, and battery fields. Use pairwise encryption on the production mesh; this
receiver's log format is only an interim bring-up surface.

Subject IDs are the local synastry-pair map. For the current Daniel receiver:

- `1` = Camille
- `2` = Daniel

The receiver maintains the latest wellness state per subject in memory. Camille's
state is reported as:

- `waiting_for_wellness` before the first Camille `WELL` packet arrives.
- `fresh` while the latest Camille packet is less than 120 seconds old.
- `stale` after 120 seconds without an update.

`tts_context` is deliberately compact so the next layer can pass it to family
synastry TTS guidance without asking the receiver to make medical claims.
