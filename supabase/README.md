# Supabase Edge Functions (Astrolabe)

Firmware calls **`voice-pipeline`** for STT → Gemini → TTS. Source is kept in this repo next to the watch sketch; deploy to the **same Supabase project** as [mynah](https://github.com/CastaliaInstitute/mynah) (`MYNAH_SUPABASE_URL` in `include/secrets.local.h`).

## Deploy

```bash
cd supabase
supabase link --project-ref <your-project-ref>
supabase secrets set \
  GOOGLE_SPEECH_API_KEY=... \
  GOOGLE_GEMINI_API_KEY=... \
  GOOGLE_TTS_API_KEY=...
# Optional: CalDAV for clock_agenda face
supabase secrets set CALDAV_URL=... CALDAV_USER=... CALDAV_PASSWORD=...

supabase functions deploy voice-pipeline
supabase functions deploy faculty-bust
```

JWT verification is on (`config.toml`). The watch sends Supabase `apikey` + Castalia `Authorization` when signed in.

## `voice-pipeline` contract

**POST** JSON (any of):

| Field | Purpose |
|-------|---------|
| `message` | Text turn (astro boot reading, typed line) |
| `audioBase64` + `sampleRateHertz` | 16 kHz mono LINEAR16 STT |
| `systemInstruction` | Gemini system prompt (astro chart context) |
| `face` | `clock_agenda` + `epochSeconds`; **`daily_briefing`** + optional `briefingFacts` (watch home tap / auto brief) |
| `briefingFacts` | Device-built astrology/synastry/moon/flash text for `daily_briefing` |
| `responseFormat` | `json` (default) or `mp3` |

**Response**

- **`json`**: `{ transcript, reply, audioBase64, route: "voice-pipeline" }` (firmware default until MP3 mode is enabled).
- **`mp3`**: `Content-Type: audio/mpeg` body; full text in `X-Voice-Transcript` / `X-Voice-Reply` (URI-encoded). ~33% smaller than base64-in-JSON.

Also honored: `Accept: audio/mpeg`.

## Watch-oriented TTS limits

Long astro readings were producing multi‑minute MP3s and huge downloads. This tree adds:

- **`capTextForWatchTts`** — caps spoken text (~1400 chars, ~90s). Full `reply` stays in JSON; only TTS input is trimmed.
- **`GEMINI_MAX_OUTPUT_TOKENS`** — optional env override; auto **512** when the system prompt mentions a mini-reading / 90 seconds.
- **`MYNAH_TTS_MAX_CHARS`** — override TTS cap.

Set in Supabase secrets or project env.

## Firmware

With `MYNAH_VOICE_RESPONSE_MP3` (see `pm_config.h`), Astrolabe POSTs `"responseFormat":"mp3"` and reads raw MPEG instead of buffering a giant JSON document.

Canonical copy also lives under `mynah/supabase/functions/voice-pipeline/`; merge improvements both ways when changing behavior.

## `faculty-bust` contract

Astrolabe downloads faculty portraits from:

```text
GET /functions/v1/faculty-bust?faculty=a.einstein&w=192&h=240&q=72
```

The function looks in Supabase Storage bucket `faculty` by default, under
`{FACULTY_BUST_PREFIX:-busts}`. It tries `{slug}/bust.jpg`,
`{slug}/bust.jpeg`, `{slug}/bust.png`, then `{slug}/bust.webp`, plus simple
aliases like `a.einstein` → `einstein`, and falls back to
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
