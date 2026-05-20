# family-rhythm-today

Authenticated Edge Function for Astrolabe devices to read the current family's private Gazetteer rhythm artifact without exposing GitHub access to firmware.

## Request

```http
GET /functions/v1/family-rhythm-today?date=YYYY-MM-DD
Authorization: Bearer <Castalia JWT>
apikey: <Supabase anon key>
```

`POST` with `{ "date": "YYYY-MM-DD" }` is also accepted.

## Server Env

- `SUPABASE_URL`
- `SUPABASE_ANON_KEY`
- `GITHUB_TOKEN` or `FAMILY_RHYTHM_GITHUB_TOKEN`: read access to private `castalia-family-*` repos
- `FAMILY_RHYTHM_REPO_MAP_JSON`
- `GITHUB_ORG` optional, defaults to `CastaliaInstitute`
- `FAMILY_RHYTHM_GITHUB_REF` optional, defaults to `main`

Mapping shape:

```json
{
  "user_ids": {
    "supabase-user-uuid": "castalia-family-example"
  },
  "emails": {
    "guardian@example.com": "CastaliaInstitute/castalia-family-example"
  }
}
```

Prefer `user_ids` for production. Use `emails` for bootstrap only.

## Response

The response intentionally contains only derived rhythm cues from `outputs/YYYY-MM-DD/synastry-rhythms.json`, not raw family config, birth dates, repo names, or GitHub metadata.

```json
{
  "ok": true,
  "configured": true,
  "date": "2026-05-19",
  "household": {
    "headline": "Deepening household rhythm",
    "summary": "Keep the day observable, kind, and adjustable.",
    "parentPractice": "Name the rhythm, then offer one concrete next step."
  },
  "windows": [
    { "label": "Late Morning", "text": "Best window for Jo, Amy." }
  ],
  "people": [
    {
      "key": "jo",
      "name": "Jo",
      "pulse": "active focus",
      "bestWindow": "late morning",
      "cue": "Use movement, humor, and quick wins before deep focus."
    }
  ],
  "updatedAt": "2026-05-20T00:30:03Z"
}
```
