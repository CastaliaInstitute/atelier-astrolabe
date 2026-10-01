# Goal: ble-audio-bridge

## Outcome

Astrolabe streams mic audio to and TTS audio from the Mynah Android app over BLE as the primary connectivity, with the app relaying to Castalia voice-stream; WiFi reserved for OTA only

## Success metrics

- <A measurable signal that proves the goal is achieved>

## Constraints

- <Budgets, deadlines, non-goals>

## Specs

<!-- Machine-parsed list: one spec per line, dependency order top to bottom.
     - <feature>                     no dependencies, start here
     - <feature> (depends: a, b)     starts only when a and b are complete
     Specs themselves live in .opencode/specs/<feature>/ and each goes through the
     three-gate workflow (requirements -> design -> tasks -> implement). -->

- ble-audio-protocol
- ble-audio-mic-stream (depends: ble-audio-protocol)
- ble-audio-tts-playback (depends: ble-audio-protocol, ble-audio-mic-stream)
- ble-audio-mynah-app (depends: ble-audio-protocol)
- voice-stream-codec