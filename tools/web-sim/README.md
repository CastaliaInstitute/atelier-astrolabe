# Astrolabe LVGL Web Simulator

This simulator builds the ESP-IDF Faculty175 face sources for the browser. It
links the real `faculty175_lvgl.c`, `faculty175_face_*.c`, and face catalog,
then renders firmware LVGL flushes and fallback `faculty175_display_*` drawing
calls into an Emscripten canvas framebuffer.

## Run

Install and activate Emscripten, then build from the repository root:

```bash
./tools/web-sim/build.sh
./tools/web-sim/serve.sh
```

Open <http://localhost:8088/astrolabe-web-sim.html>.

If Emscripten is not installed locally but Docker or Podman is running:

```bash
./tools/web-sim/build-container.sh
./tools/web-sim/serve.sh
```

## Voice Testing

The simulator includes browser-side voice controls so face behavior can be
tested without flashing hardware:

- **STT** uses the browser SpeechRecognition API when available. If the browser
  does not expose STT, select **Manual transcript + Browser TTS** and edit the
  transcript text directly.
- **TTS** uses `speechSynthesis` and reports `tts.start`, `tts.end`, or
  `tts.error` events in the voice report pane.
- **Duplex** runs one listen/reply turn. Enable **Continuous** before pressing
  **Duplex** to loop STT then TTS until **Stop** is pressed.
- **Boot/TTS** simulates the firmware BOOT/TTS button for the selected face.
- **All Faces** simulates BOOT/TTS for every face and writes pass/fail events
  to the report pane. The same lines are exposed to browser automation as
  `window.astrolabeVoiceTestReport`.
- Browser automation can set `window.astrolabeWebsimVoiceSink(text, detail)` to
  simulate TTS completion in headless environments where native speech output is
  unavailable or not mockable.

## Architecture

- `tools/web-sim/CMakeLists.txt` globs and compiles
  `faculty175/main/faculty175_face_*.c`, `faculty175_lvgl.c`, and
  `faculty175_faces.c`.
- `tools/web-sim/src/main.c` owns the Emscripten main loop, current face ID,
  dispatch call, and exported face catalog hooks used by the browser controls.
- `tools/web-sim/src/faculty175_websim_stubs.c` provides the browser
  framebuffer implementation of `faculty175_display_*` and narrow ESP-IDF data
  stubs for NVS, time, board audio, touch, Wi-Fi settings, charts, almanac,
  quotes, rockets, faculty busts, and tarot images.
- `faculty175_face_dispatch_draw()` follows the firmware order: real LVGL faces
  render first through `faculty175_lvgl_draw_face()`, and unsupported faces fall
  through to compiled native face drawing functions where those exist.

The simulator exposes the current ESP-IDF Faculty175 face ID catalog from the
firmware metadata. Device storage and network assets that are not available in
the browser use deterministic demo data or graceful image fallbacks.
