## Summary

New **`ClockFace::Faculty`** for on-watch **ask-faculty** conversations (Phase 2 voice parity with Android [`GlowScreen`](https://github.com/CastaliaInstitute/mynah/blob/main/android/app/src/main/java/institute/castalia/mynah/ui/GlowScreen.kt)).

**User flow**

1. User swipes to the Faculty face (or scrolls to a faculty — see below).
2. **PWR hold / PTT** → capture **STT** (16 kHz mono PCM via existing `pm_voice` / `voice-pipeline` path).
3. Utterance may name a faculty (“ask Einstein…”, “what would Curie say about…”) or continue the **currently selected** faculty.
4. **Router step** (edge function or `voice-pipeline` routing) resolves **`facultySlug`** from the transcript when needed.
5. Device calls **`ask-faculty`** with `facultySlug` and the user question. **Conversation history** is **not stored on the watch** — Castalia/commonplace holds prior turns; the edge function (or client pulling recent commonplace entries) assembles history server-side for the prompt.
6. On success:
   - **`GET` [`faculty-bust`](https://github.com/CastaliaInstitute/mynah/blob/main/supabase/functions/faculty-bust/index.ts)** with `faculty` query param → download/cache portrait (flash or PSRAM, `pm_faculty_bust`) and show on the round display.
   - Play **LLM reply + TTS** from the response (`audioBase64` MP3 when present; text-only path already supported).
7. Optional: brief on-face status for the **current** exchange only (listening / thinking / speaking) — not a scrollable transcript archive.

**On-device state (faculty + bust only)**

| Stored locally | Not stored locally |
|----------------|-------------------|
| Ordered list of **recent faculty slugs** (NVS, capped) | Full conversation text / turn log |
| **Cached bust image** per slug (`pm_faculty_bust`) | Commonplace payloads |
| **Active faculty** index into the recent list | Server-side history assembly |

**Speaker history (UI)**

- **Swipe up/down** on this face: scroll through **recent speakers** (faculty slugs), not conversation messages.
- Each step shows that faculty’s **cached bust** (fetch on first use, refresh if stale/missing).
- Selecting a faculty sets the **active** slug for the next PTT; `ask-faculty` continues that thread on Castalia using server-held history for that `facultySlug` + user.
- New faculty from STT: append slug to recent list, download bust, make active.

**Castalia (source of truth for conversations)**

- Turns logged via [`commonplaceDirectus`](https://github.com/CastaliaInstitute/mynah/blob/main/supabase/functions/_shared/commonplaceDirectus.ts) (`kind: conversation`, `route: ask-faculty`, `facultySlug`) on the server.
- Firmware does **not** implement local conversation scrollback; v1 does **not** require on-watch commonplace fetch UI.

## Backlog reference

`docs/BACKLOG.md` — **P1 Faculty face** (Phase 2: round UI + voice parity). See also [`docs/pocketwatch.md` § Backend](docs/pocketwatch.md#backend-reuse).

## Acceptance criteria

- [ ] Faculty face in swipe cycle; PTT → STT → `ask-faculty` → TTS for **active** faculty.
- [ ] **No local conversation archive** — only recent **faculty slugs** + bust cache in NVS/flash.
- [ ] Swipe up/down scrolls **speaker history** (recent faculty); bust shown per speaker.
- [ ] `faculty-bust` fetched and cached per slug; bust updates when active speaker changes.
- [ ] `./scripts/build.sh` passes; `docs/BACKLOG.md` has `Issue: #N`.
- [ ] **Hardware QA**: screenshot with bust visible while browsing at least two recent faculty.

## Dependencies / notes

- Reuses `pm_castalia_auth`, `pm_voice` / `voice-pipeline`, text-only `ask-faculty` path (backlog done).
- Mynah: `voice-pipeline`, `ask-faculty`, `faculty-bust`.
- Out of scope v1: on-watch transcript history UI; wake word; full commonplace browser.

## Branch

`feature/<issue#>-faculty-face` from **`integration`**. PR targets **`integration`** with `Closes #<issue#>`.
