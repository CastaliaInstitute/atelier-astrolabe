# Hafez face backend contract (Supabase)

The firmware `ClockFace::Hafez` calls:

- `POST /functions/v1/hafez-daily`

Request body:

```json
{
  "epochSeconds": 1715904000
}
```

`epochSeconds` is optional; when omitted, the function uses current UTC time.

Response body (success):

```json
{
  "ok": true,
  "configured": true,
  "dayKey": 20260518,
  "quote": "I wish I could show you...",
  "source": "Hafez",
  "artPrompt": "A luminous midnight garden...",
  "artSeed": 130913,
  "palette": "midnight-indigo"
}
```

## Table

Migration: `supabase/migrations/20260518034700_hafez_quotes.sql`

- `public.hafez_quotes`
- selector RPC: `public.pick_hafez_quote(p_day date, p_locale text)`

The selector is deterministic per UTC date (`p_day`) so all devices get the same quote/art seed for the day.

## Deploy

```bash
supabase db push
supabase functions deploy hafez-daily
```

The function requires:

- `SUPABASE_URL`
- `SUPABASE_SERVICE_ROLE_KEY`
