# Astrolabe audio pipeline

Reusable ESP-IDF voice pipeline for Astrolabe devices.

This component owns the shared voice turn architecture:

- reads board PCM through `astrolabe_audio_read_fn`
- detects speech with RMS VAD
- queues one captured turn at a time
- posts PCM as base64 JSON to the configured faculty endpoint
- emits transcript/reply/status events to the app layer
- decodes response MP3 with minimp3
- writes decoded PCM through `astrolabe_audio_write_fn`

Board-specific code stays outside the component. A device supplies the I2S,
codec, display, and UI adapters through `astrolabe_audio_pipeline_config_t`.
That keeps the native ESP-IDF baseline aligned with the AtomS3R faculty
prototype without baking AtomS3R pinout, display, or codec assumptions into the
pipeline.

Minimum integration shape:

1. Initialize board audio and networking.
2. Fill `astrolabe_audio_pipeline_config_t` with read/write/rate/mute callbacks.
3. Set `endpoint_url`, optional Supabase `api_key`, and current face/faculty
   metadata.
4. Call `astrolabe_audio_pipeline_create()` and
   `astrolabe_audio_pipeline_start()`.
5. Update display/UI from `on_event`.
