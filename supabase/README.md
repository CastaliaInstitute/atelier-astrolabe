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
supabase functions deploy ask-faculty-voice
supabase functions deploy faculty-bust
```

JWT verification is on (`config.toml`). The watch sends Supabase `apikey` + Castalia `Authorization` when signed in.

## Device Provisioning

Faculty 1.75C firmware also sends per-device HMAC headers:

| Header | Purpose |
|--------|---------|
| `X-Astrolabe-Device-Mac` | efuse MAC, normalized lowercase |
| `X-Astrolabe-Device-Nonce` | random 16-byte nonce as hex |
| `X-Astrolabe-Device-Channel` | firmware channel, e.g. `astrolabe-faculty-amoled175` |
| `X-Astrolabe-Device-Signature` | HMAC-SHA256 over `mac\nnonce\nchannel\n` |

Provisioning flow:

```bash
supabase db push
supabase secrets set ASTROLABE_DEVICE_AUTH_REQUIRED=true

# On device serial:
device provision

# On workstation, using the printed mac/secret:
./scripts/provision-faculty175-device.py \
  --mac a0:f2:62:e3:06:44 \
  --secret <64-hex-secret> \
  --label "faculty175 lab unit"
```

This protects privileged Castalia pipeline access from arbitrary boards with only the public firmware. It does not replace ESP secure boot + flash encryption for physical attacker resistance.

## `voice-pipeline` contract

**POST** JSON (any of):

| Field | Purpose |
|-------|---------|
| `message` | Text turn (astro boot reading, typed line) |
| `audioBase64` + `sampleRateHertz` | 16 kHz mono LINEAR16 STT |
| `systemInstruction` | Gemini system prompt (astro chart context) |
| `face` | `clock_agenda` + `epochSeconds`; **`daily_briefing`** + optional `briefingFacts` (watch home tap / auto brief) |
| `briefingFacts` | Device-built astrology/synastry/moon/flash text for `daily_briefing` |
| `facultySlug` / `facultyName` | Select Castalia faculty metadata, Chirp 3 HD Google TTS voice, and delivery prompt |
| `ttsVoiceName` / `ttsVoice` | Direct Google TTS override for QA or local scripted tours; prefer faculty fields for product tours |
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

## Chirp 3 HD TTS

Default watch / Castalia TTS uses **Google Chirp 3: HD** (`en-US-Chirp3-HD-Charon` unless overridden). Faculty rows in Supabase may set:

| Column | Purpose |
|--------|---------|
| `google_tts_voice_name` | e.g. `en-US-Chirp3-HD-Kore` |
| `google_tts_language_code` | e.g. `en-US`, `en-GB` |
| `google_tts_prompt` | Chirp 3 style / delivery instruction (`SynthesisInput.prompt`) |
| `voice_prompt` | LLM system prompt for **`ask-faculty`** (corpus-tuned teaching voice) |

Edge functions fall back to [`_shared/facultyTts.ts`](functions/_shared/facultyTts.ts) when columns are empty. Apply migration on the shared Castalia Supabase project from **castalia.institute**:

```bash
cd ../castalia.institute/supabase   # or clone CastaliaInstitute/castalia.institute
supabase db push
```

Migration: `20260529120000_faculty_chirp3_tts.sql`.

Optional env overrides per faculty slug: `FACULTY_TTS_VOICE_<SLUG>`, `FACULTY_TTS_LANGUAGE_<SLUG>`, `FACULTY_TTS_PROMPT_<SLUG>` (slug uppercased, `.` → `_`).

## TTS usage / cost by user

Each successful Google TTS synthesis logs a row to **`voice_usage_events`** (castalia.institute migration `20260529130000_voice_usage_events.sql`):

| Column | Meaning |
|--------|---------|
| `user_id` | Supabase auth user from `Authorization` JWT (null if anon-key only) |
| `billable_chars` | Spoken + style prompt characters |
| `estimated_usd` | Chirp 3 default $0.00003/char (override `VOICE_TTS_USD_PER_CHAR`) |

Daily rollup view: **`voice_usage_daily_by_user`**. Disable logging with `VOICE_USAGE_DISABLED=true`.

Set in Supabase secrets or project env.

## Firmware

With `MYNAH_VOICE_RESPONSE_MP3` (see `pm_config.h`), Astrolabe POSTs `"responseFormat":"mp3"` and reads raw MPEG instead of buffering a giant JSON document.

Canonical copy also lives under `mynah/supabase/functions/voice-pipeline/`; merge improvements both ways when changing behavior.

## `faculty-bust` contract

Astrolabe downloads faculty portraits from:

```text
GET /functions/v1/faculty-bust?faculty=a.einstein&w=128&h=128&q=60
```

Query params:

| Param | Aliases | Purpose |
|-------|---------|---------|
| `faculty` | `handle`, `slug` | Faculty id (`a.einstein`, `a-einstein`, `einstein`) |
| `w` | `width` | Output width in px (48–320, default 192) |
| `h` | `height` | Output height in px (48–360, default 240) |
| `q` | `quality` | JPEG quality when `format=jpeg` (35–90, default 72) |
| `format` | — | `png` (default, transparent) or `jpeg` (black letterbox) |
| `resize` | — | `contain` (default), `cover`, or `fill` |

**Response:** **`image/png`** with alpha by default (transparent letterbox for AMOLED clients). Pass `format=jpeg` for legacy black-background JPEG.

If the Storage object is a Castalia **avatar sprite sheet** (128×128 or 256×256 PNG with four busts in a 2×2 grid), the function crops the **lower-left** portrait cell before resizing — clients always receive a single bust, never the 4-up mosaic. Response header `X-Faculty-Bust-Sprite-Cell: lower-left` when that path runs.

Legacy deployments that returned full-size PNG for `handle=` must be replaced — redeploy this tree:

```bash
cd supabase
supabase functions deploy faculty-bust
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

The Edge Function signs the Storage object, decodes PNG/JPEG/WebP with ImageScript,
and returns a **small PNG or JPEG** at the requested size. Keep source assets under
`busts/{slug}/bust.*`; JPEG sources are ideal but PNG/WebP are resized server-side.

Optional secrets:

```bash
supabase secrets set \
  FACULTY_BUST_BUCKET=faculty \
  FACULTY_BUST_PREFIX=busts \
  FACULTY_DEFAULT_BUST_SLUG=einstein \
  FACULTY_BUST_EXTENSIONS=jpg,jpeg,png,webp \
  FACULTY_BUST_SIGN_TTL_SEC=3600
```
