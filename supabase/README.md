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

# Optional: Commonplace journal logging. Directus is backed by the same
# Supabase database, so the functions can write with SUPABASE_SERVICE_ROLE_KEY
# when DIRECTUS_STATIC_TOKEN is not configured.
supabase secrets set \
  DIRECTUS_URL=https://commonplace-directus-652016456291.us-central1.run.app \
  MYNAH_COMMONPLACE_AUTHOR_SLUG=custodian \
  MYNAH_COMMONPLACE_STATUS=draft \
  MYNAH_COMMONPLACE_VISIBILITY=private

supabase functions deploy voice-pipeline
supabase functions deploy voice-stream
supabase functions deploy ask-faculty-voice
supabase functions deploy faculty-dreams
supabase functions deploy faculty-bust
supabase functions deploy astrolabe-device-lookup
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
./scripts/provision-astrolabe175c-device.py \
  --mac a0:f2:62:e3:06:44 \
  --secret <64-hex-secret> \
  --label "faculty175 lab unit"
```

This protects privileged Castalia pipeline access from arbitrary boards with only the public firmware. It does not replace ESP secure boot + flash encryption for physical attacker resistance.

Provisioning also stores `short_id`, a four-hex display/lookup ID matching the
firmware BLE radar hash. Override it only when repairing a registry collision:

```bash
./scripts/provision-astrolabe175c-device.py \
  --mac a0:f2:62:e3:07:9c \
  --secret <64-hex-secret> \
  --label "Daniel Astrolabe" \
  --short-id 1a2b
```

Authenticated clients can resolve a short ID without receiving the full MAC:

```bash
curl "$SUPABASE_URL/functions/v1/astrolabe-device-lookup?shortId=1a2b&channel=astrolabe-faculty-amoled175" \
  -H "Authorization: Bearer $SUPABASE_ANON_KEY" \
  -H "apikey: $SUPABASE_ANON_KEY"
```

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

## `voice-stream` contract

WebSocket endpoint:

```text
wss://<project-ref>.supabase.co/functions/v1/voice-stream
```

The socket follows an OpenAI Realtime-style event model:

- client sends `session.update`
- client streams PCM with `input_audio_buffer.append` or binary PCM frames
- client/server commits the input buffer
- server emits transcript, text, audio, completion, and error events

Session flags:

- `interactionMode: "conversation"`: normal STT → LLM/TTS response.
- `interactionMode: "transcribe"`: STT only; no immediate reply.
- `interactionMode: "journal"`: STT only and default Commonplace journal write.
- `commonplaceMode: "off" | "conversation" | "journal"` and
  `logToCommonplace: boolean` control Commonplace writes.

Faculty conversations write realtime transcript/reply works to Commonplace.
Later `ask-faculty-voice` turns embed the current request with
`gemini-embedding-001`, search `faculty_memory_embeddings` by vector similarity
for the same account/session and faculty slug, then inject the relevant memory
context into the faculty prompt. If vector search is unavailable, it falls back
to scoped Commonplace text retrieval. Disable retrieval with
`MYNAH_COMMONPLACE_MEMORY_DISABLED=true`; tune the vector gate with
`MYNAH_COMMONPLACE_MEMORY_VECTOR_THRESHOLD` (default `0.35`); disable all
Commonplace writes with `MYNAH_COMMONPLACE_DISABLED=true`.

Durable memory is formed by `faculty-dreams`, a nightly dream worker that scans
recent realtime `Mynah conversation` works, groups them by user/session +
faculty + day, and writes traceable `Faculty memory` notes with source work IDs.
Each created memory is embedded for vector RAG. Existing memory notes can be
backfilled with `embedExisting:true`.
Invoke it with a service-role JWT:

```bash
curl -X POST "$SUPABASE_URL/functions/v1/faculty-dreams" \
  -H "Authorization: Bearer $SUPABASE_SERVICE_ROLE_KEY" \
  -H "apikey: $SUPABASE_SERVICE_ROLE_KEY" \
  -H "Content-Type: application/json" \
  --data '{"windowHours":30}'
```

```bash
curl -X POST "$SUPABASE_URL/functions/v1/faculty-dreams" \
  -H "Authorization: Bearer $SUPABASE_SERVICE_ROLE_KEY" \
  -H "apikey: $SUPABASE_SERVICE_ROLE_KEY" \
  -H "Content-Type: application/json" \
  --data '{"embedExisting":true,"embedLimit":100}'
```

`ask-faculty-voice` can also be used for writing and non-speech chat. Send
`generateTts:false` (or `tts:false`) to return text without `audioBase64`, or
`responseFormat:"text"` for a plain-text response. Send
`logToCommonplace:false` or `commonplaceMode:"off"` when a text exchange should
not be archived.

Firmware should still write capture audio into a flash circular buffer before or
while sending over the socket. See
[`docs/design/streaming-audio-socket.md`](../docs/design/streaming-audio-socket.md).

### Local `voice-stream` Mock QA

For firmware and host-side rolling-buffer tests that should not depend on cloud
STT/TTS latency, run the local mock WebSocket server:

```bash
python3 scripts/voice_stream_mock_relay.py --host 0.0.0.0 --port 8788
```

The mock accepts the same `session.update`, binary PCM frame, and
`input_audio_buffer.commit` events as `voice-stream`, accumulates non-final
segments with a configurable high cap, then emits transcript, text, MP3 audio
delta, and `response.done` on the final commit.

Host-side long-turn validation:

```bash
node scripts/voice_stream_persistent_validate.mjs \
  --ws-url ws://127.0.0.1:8788/functions/v1/voice-stream \
  --synthetic-seconds 70 \
  --segments 140 \
  --segment-pause-ms 0
```

For 1.75C device testing, build with a LAN-reachable URL:

```bash
ASTROLABE175C_VOICE_STREAM_URL=ws://<host-ip>:8788/functions/v1/voice-stream \
./scripts/astrolabe175c_build.sh build
```

Then trigger `pipeline capture 0` over serial. The device should keep listening,
roll flash-backed 500 ms capture slots, send non-final commits, receive a final
mock TTS response, and play it without growing a full-turn PSRAM buffer.

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

## Voice usage / cost by user

Each successful metered voice service call logs a row to **`voice_usage_events`**
(castalia.institute migrations `20260529130000_voice_usage_events.sql` and
`20260602190000_voice_usage_cost_gates.sql`):

| Column | Meaning |
|--------|---------|
| `user_id` | Supabase auth user from `Authorization` JWT (null if anon-key only) |
| `service` | `google_stt`, `google_gemini`, or `google_tts` |
| `audio_seconds` | STT input duration |
| `input_tokens` / `output_tokens` | Estimated LLM token counts |
| `billable_chars` | TTS spoken + style prompt characters |
| `estimated_usd` | Estimated provider cost for the event |

Cost gates run before STT, Gemini, and TTS calls. Set any of these env vars to
a positive USD value to enable that gate:

| Env var | Meaning |
|---------|---------|
| `VOICE_USAGE_DAILY_USD_LIMIT` | Per-authenticated-user daily cap |
| `VOICE_USAGE_MONTHLY_USD_LIMIT` | Per-authenticated-user monthly cap |
| `VOICE_USAGE_UNAUTH_DAILY_USD_LIMIT` | Shared daily cap for anon/device-only calls |
| `VOICE_USAGE_UNAUTH_MONTHLY_USD_LIMIT` | Shared monthly cap for anon/device-only calls |
| `VOICE_USAGE_REQUIRE_USER_FOR_METERED=true` | Reject metered calls without a user JWT |
| `VOICE_USAGE_GATES_DISABLED=true` | Disable gates while still logging usage |

Pricing defaults can be overridden without code changes:
`VOICE_STT_USD_PER_MINUTE` (default `0.024`), `VOICE_TTS_USD_PER_CHAR`
(Chirp 3 default `0.00003`), `VOICE_GEMINI_USD_PER_INPUT_TOKEN`, and
`VOICE_GEMINI_USD_PER_OUTPUT_TOKEN`.

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
| `w` | `width` | Output width in px (48–512, default 192) |
| `h` | `height` | Output height in px (48–512, default 240) |
| `size` | — | Optional square shortcut (`380x380` or `380`) |
| `q` | `quality` | JPEG quality when `format=jpeg` (35–90, default 72) |
| `format` | — | `png` (default, transparent) or `jpeg` (black letterbox) |
| `resize` | — | `contain` (default), `cover`, or `fill` |
| `view` | `variant` | `right` (default), `frontal`, or `line` |

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
`{FACULTY_DEFAULT_BUST_SLUG:-darwin}/bust.*`.

For color e-paper clients, request `view=line`:

```text
GET /functions/v1/faculty-bust?faculty=a.einstein&w=320&h=320&resize=cover&format=png&view=line
```

`view=line` first signs `faculty.line_bust_path`. If the row has no cached
line bust, the function generates a monochrome line-art PNG from the regular
bust, uploads it to Storage as `{slug}/line_bust.png`, writes that path back to
`faculty.line_bust_path`, and serves the cached asset on future requests.

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
  FACULTY_DEFAULT_BUST_SLUG=darwin \
  FACULTY_BUST_EXTENSIONS=jpg,jpeg,png,webp \
  FACULTY_BUST_SIGN_TTL_SEC=3600
```
