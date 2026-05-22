# ESP-IDF Heap Migration TODO

Goal: reduce internal heap pressure while moving Astrolabe away from Arduino runtime wrappers in small, testable slices.

## Phase 1: Measure and Stop Avoidable Allocations

- [x] Add a per-face heap telemetry line for enter, draw, network fetch, voice start, voice end, and leave.
- [x] Track free internal heap, largest internal block, free PSRAM, and largest PSRAM block.
- [x] Add a resource governor so TTS, STT, media stream, bust fetch, and analyzer/AEC cannot all own heavy buffers at once.
- [x] Move simple persistent settings from Arduino `Preferences` to direct ESP-IDF NVS.
- [x] Remove `String` use from boot and simple persistence paths where fixed buffers are enough.

## Phase 2: Network and TLS

- [x] Add one shared ESP-IDF HTTP helper around `esp_http_client`.
- [x] Support callback streaming, fixed-cap JSON reads, and direct-to-flash downloads.
- [x] Port small JSON clients first: weather, quotes, Calcifer, Spotify status.
- [x] Port binary/streaming clients next: faculty busts, voice MP3, rocket media.
- [x] Tune mbedTLS fragment sizes in `sdkconfig` after HTTPClient is gone.

## Phase 3: Face Memory Lifecycle

- [x] Give every face explicit `enter()`, `tick()`, `draw()`, and `leave()` hooks.
- [ ] Add a face scratch arena that is reset on face leave.
- [ ] Ensure image/audio decoded buffers are released on face leave.
- [ ] Keep one decoded faculty/quote bust resident at a time.
- [ ] Stream JPEG decode from flash where possible.

## Phase 4: Arduino Runtime Removal

- [ ] Replace Arduino `WebServer` with `esp_http_server`.
- [ ] Replace Arduino WiFi calls with `esp_wifi`, `esp_netif`, and `esp_event`.
- [ ] Replace Arduino time/network glue with IDF SNTP and netif APIs.
- [x] Remove direct dependency on Arduino `WiFiClient`/`WiFiClientSecure`.
- [ ] Remove Arduino framework from the default build after display/touch/audio dependencies are isolated or replaced.

## Gates

- [ ] `pio run -e waveshare_s3_175`
- [ ] Flash smoke test.
- [ ] Screen HTTP still serves `/screen.bmp`.
- [ ] Faculty, Quotes, Rocket, Tour TTS, Notes mic, and Settings WiFi still work.
- [ ] Compare heap telemetry before/after each phase.
