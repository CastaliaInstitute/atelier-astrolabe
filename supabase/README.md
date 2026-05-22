# Supabase Edge Functions (Astrolabe)

Firmware calls **`voice-pipeline`** for STT → Gemini → TTS. Source is kept in
this repo next to the watch sketch; deploy to the **same Supabase project** as
[mynah](https://github.com/CastaliaInstitute/mynah) (`MYNAH_SUPABASE_URL` in
`include/secrets.local.h`).

## Deploy

```bash
cd supabase
supabase link --project-ref <your-project-ref>
supabase secrets set \
  GOOGLE_SPEECH_API_KEY=... \
  GOOGLE_GEMINI_API_KEY=... \
  GOOGLE_TTS_API_KEY=...
# Optional but recommended for `voice-stream` WebSocket STT:
# enable Cloud Speech-to-Text in the service-account project, then set one of:
supabase secrets set GOOGLE_STT_SERVICE_ACCOUNT_JSON='{"project_id":"...","client_email":"...","private_key":"..."}'
# or reuse the existing Vertex service account secret:
# VERTEX_SERVICE_ACCOUNT_JSON='{"project_id":"...","client_email":"...","private_key":"..."}'
# Optional: CalDAV for clock_agenda face
supabase secrets set CALDAV_URL=... CALDAV_USER=... CALDAV_PASSWORD=...

supabase functions deploy voice-pipeline
supabase functions deploy voice-stream
supabase functions deploy media-stream
supabase functions deploy ask-faculty
supabase functions deploy faculty-bust
```

JWT verification is on for private voice/faculty functions (`config.toml`). The
watch sends Supabase `apikey` + Castalia `Authorization` when signed in.
`media-stream` is intentionally public so a phone camera can open QR redirect
links without auth headers.

## `voice-pipeline` contract

**POST** JSON (any of):

| Field                             | Purpose                                                                                                        |
| --------------------------------- | -------------------------------------------------------------------------------------------------------------- |
| `message`                         | Text turn (astro boot reading, typed line)                                                                     |
| `audioBase64` + `sampleRateHertz` | 16 kHz mono LINEAR16 STT                                                                                       |
| `systemInstruction`               | Gemini system prompt (astro chart context)                                                                     |
| `face`                            | `clock_agenda` + `epochSeconds`; **`daily_briefing`** + optional `briefingFacts` (watch home tap / auto brief) |
| `briefingFacts`                   | Device-built astrology/synastry/moon/flash text for `daily_briefing`                                           |
| `responseFormat`                  | `json` (default) or `mp3`                                                                                      |

**Response**

- **`json`**: `{ transcript, reply, audioBase64, route: "voice-pipeline" }`
  (firmware default until MP3 mode is enabled).
- **`mp3`**: `Content-Type: audio/mpeg` body; full text in `X-Voice-Transcript`
  / `X-Voice-Reply` (URI-encoded). ~33% smaller than base64-in-JSON.

Also honored: `Accept: audio/mpeg`.

## `voice-stream` contract

`voice-stream` is the watch-oriented streaming transport. It keeps the speech
contract centralized by forwarding committed turns to `voice-pipeline` with
`responseFormat: "mp3"`.

### WebSocket

Connect to:

```text
wss://<project>.supabase.co/functions/v1/voice-stream?sampleRateHertz=16000&languageCode=en-US
```

Send:

- JSON config:
  `{ "type": "start", "sampleRateHertz": 16000, "languageCode": "en-US", "face": "faculty", "facultySlug": "a.einstein" }`
- Binary frames: raw mono LINEAR16 PCM chunks.
- Commit: `{ "type": "commit" }`

Receive:

- JSON events: `ready`, `ack`, `processing`, `metadata`, `done`, `error`.
- Binary frames between `metadata` and `done`: raw MP3 bytes for playback.

This is currently a streaming transport over the existing REST STT path: audio
chunks are collected until `commit`, then processed by `voice-pipeline`. When a
Google service-account JSON secret is present, the WebSocket starts a Google
gRPC `streamingRecognize` session and emits `transcript` events while audio is
arriving. On commit, if Google did not produce a final streaming result,
`voice-stream` falls back to Google REST `speech:recognize` using the same
service-account OAuth token and then forwards the final transcript to
`voice-pipeline` for LLM/TTS.

Required Google setup for live STT:

- Enable `speech.googleapis.com` in the service-account project.
- Grant the service account permission to use Cloud Speech-to-Text.
- Set `GOOGLE_STT_SERVICE_ACCOUNT_JSON`, `GOOGLE_APPLICATION_CREDENTIALS_JSON`,
  or `VERTEX_SERVICE_ACCOUNT_JSON` in Supabase secrets.

### HTTP

For simpler firmware testing:

```bash
curl -X POST \
  -H "Authorization: Bearer $JWT" \
  -H "apikey: $ANON_KEY" \
  -H "Content-Type: application/octet-stream" \
  -H "x-sample-rate-hertz: 16000" \
  --data-binary @utterance.s16le \
  "https://<project>.supabase.co/functions/v1/voice-stream" \
  -o reply.mp3
```

The response is `audio/mpeg`; transcript/reply metadata is returned in
`X-Voice-*` headers.

## `media-stream` contract

`media-stream` is the watch-oriented media resolver. It does not transcode
video in Supabase; it normalizes watch/phone links into one small contract that
Astrolabe can QR or open later.

```bash
curl -G \
  --data-urlencode "url=https://www.spacex.com/launches/starship-flight-12" \
  --data-urlencode "title=Starship Flight 12" \
  "https://<project>.supabase.co/functions/v1/media-stream"
```

Response:

```json
{
  "protocol": "mynah.media-stream.v1",
  "ok": true,
  "kind": "video",
  "provider": "spacex",
  "title": "Starship Flight 12",
  "watchUrl": "https://www.spacex.com/launches/starship-flight-12",
  "qrUrl": "https://<project>.supabase.co/functions/v1/media-stream?...&redirect=1",
  "openMode": "external",
  "canProxy": false
}
```

Use `qrUrl` for a stable phone QR; the function redirects to the normalized
provider URL. YouTube URLs are normalized to canonical watch/embed URLs.

## Watch-oriented TTS limits

Long astro readings were producing multi‑minute MP3s and huge downloads. This
tree adds:

- **`capTextForWatchTts`** — caps spoken text (~1400 chars, ~90s). Full `reply`
  stays in JSON; only TTS input is trimmed.
- **`GEMINI_MAX_OUTPUT_TOKENS`** — optional env override; auto **512** when the
  system prompt mentions a mini-reading / 90 seconds.
- **`MYNAH_TTS_MAX_CHARS`** — override TTS cap.

Set in Supabase secrets or project env.

## Firmware

With `MYNAH_VOICE_RESPONSE_MP3` (see `pm_config.h`), Astrolabe POSTs
`"responseFormat":"mp3"` and reads raw MPEG instead of buffering a giant JSON
document.

Canonical copy also lives under `mynah/supabase/functions/voice-pipeline/`;
merge improvements both ways when changing behavior.

## `ask-faculty` voice metadata

`ask-faculty` reads per-faculty voice metadata from `public.faculty`:

- `google_tts_voice_name`
- `google_tts_language_code`
- `voice_ethnicity`
- `voice_accent`
- `voice_language`
- `voice_prompt`

`voice_ethnicity`, `voice_accent`, and `voice_language` are appended to the
Gemini system prompt as respectful voice-casting guidance. The function also
passes the selected Google TTS voice into synthesis and returns the resolved
metadata in `facultyVoice`.

## `faculty-bust` contract

Astrolabe downloads faculty portraits from:

```text
GET /functions/v1/faculty-bust?faculty=a.einstein&w=192&h=240&q=72
```

The function looks in Supabase Storage bucket `faculty` by default, under
`{FACULTY_BUST_PREFIX:-busts}`. It tries `{slug}/bust.jpg`, `{slug}/bust.jpeg`,
`{slug}/bust.png`, then `{slug}/bust.webp`, plus simple aliases like
`a.einstein` → `einstein`, and falls back to
`{FACULTY_DEFAULT_BUST_SLUG:-einstein}/bust.*`.

Recommended setup:

```bash
supabase storage cp ./einstein.jpg ss:///faculty/busts/einstein/bust.jpg
```

The Edge Function uses the service role to create a signed Storage URL with
Supabase server-side image transformation (`width`, `height`, `quality`,
`resize=contain`) and proxies the transformed bytes back to the watch. Keep
source assets in JPEG where possible; the firmware uses `JPEGDEC`, so JPEG
sources are the safest path even though the server will also try PNG/WebP.

Optional secrets:

```bash
supabase secrets set \
  FACULTY_BUST_BUCKET=faculty \
  FACULTY_BUST_PREFIX=busts \
  FACULTY_DEFAULT_BUST_SLUG=einstein \
  FACULTY_BUST_EXTENSIONS=jpg,jpeg,png,webp \
  FACULTY_BUST_SIGN_TTL_SEC=3600
```
