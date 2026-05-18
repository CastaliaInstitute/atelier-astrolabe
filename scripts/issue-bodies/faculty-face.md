## Summary

New **`ClockFace::Faculty`** for on-watch **ask-faculty** conversations (Phase 2 voice parity with Android [`GlowScreen`](https://github.com/CastaliaInstitute/mynah/blob/main/android/app/src/main/java/institute/castalia/mynah/ui/GlowScreen.kt)).

**User flow**

1. User swipes to the Faculty face.
2. **PWR hold / PTT** → capture **STT** (16 kHz mono PCM via existing `pm_voice` / `voice-pipeline` path).
3. Utterance may name a faculty (“ask Einstein…”, “what would Curie say about…”) or continue the active thread.
4. **Router step** (edge function or `voice-pipeline` routing) resolves **`facultySlug`** from the transcript.
5. Device calls **`ask-faculty`** with `facultySlug`, the user question, and **conversation history** for that faculty.
6. On success:
   - **`GET` [`faculty-bust`](https://github.com/CastaliaInstitute/mynah/blob/main/supabase/functions/faculty-bust/index.ts)** with `faculty` query param → download/cache portrait (flash or PSRAM, `pm_faculty_bust`) and show on the round display during the exchange.
   - Play **LLM reply + TTS** from the response (`audioBase64` MP3 when present; text-only path already supported).
7. Show a short **transcript snippet** on the face (question + reply summary).

**Navigation / state**

- **Swipe up/down** on this face: cycle **recent faculty** (NVS list of slugs last spoken with); each slug keeps its own thread.
- **Commonplace** (follow-up): log turns via `commonplaceDirectus` (`kind: conversation`, `route: ask-faculty`, `facultySlug`) and use recent entries for prompt history.

## Backlog reference

`docs/BACKLOG.md` — **P1 Faculty face** (Phase 2: round UI + voice parity). See also [`docs/pocketwatch.md` § Backend](docs/pocketwatch.md#backend-reuse).

## Acceptance criteria

- [ ] Faculty face appears in the face swipe cycle (`ClockFace::Faculty` or equivalent).
- [ ] PWR/PTT on Faculty face: STT → faculty resolution → `ask-faculty` → TTS playback on device.
- [ ] After a resolved faculty, **`faculty-bust`** portrait is fetched, cached, and drawn on the face for that session.
- [ ] Swipe up/down cycles recent faculty slugs from NVS; continuing a thread reuses history for that slug.
- [ ] `./scripts/build.sh` passes.
- [ ] `docs/BACKLOG.md` line updated with `Issue: #N`.
- [ ] **Hardware QA**: screenshot (`screen.bmp`) of Faculty face with bust visible during/after a reply.

## Dependencies / notes

- Reuses existing Castalia auth (`pm_castalia_auth`), `pm_voice` / `voice-pipeline`, and text-only `ask-faculty` handling (backlog done).
- Mynah edge functions: `voice-pipeline`, `ask-faculty`, `faculty-bust` (no new server contract required for v1 if routing already exists).
- Out of scope for v1 unless trivial: full commonplace history sync UI; wake word.

## Branch

`feature/<issue#>-faculty-face` from **`integration`**. PR targets **`integration`** with `Closes #<issue#>`.
