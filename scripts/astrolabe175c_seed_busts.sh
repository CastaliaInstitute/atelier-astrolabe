#!/usr/bin/env bash
# Generate 466px faculty busts on Supabase for the astrolabe175c default roster.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ENV_FILE="${CASTALIA_ENV:-$HOME/GitHub/CastaliaInstitute/castalia.institute/.env}"

if [[ -f "$ENV_FILE" ]]; then
  # shellcheck disable=SC1090
  set -a && source "$ENV_FILE" && set +a
fi

SUPABASE_URL="${MYNAH_SUPABASE_URL:-${SUPABASE_URL:-${NEXT_PUBLIC_SUPABASE_URL:-}}}"
SERVICE_KEY="${SUPABASE_SERVICE_ROLE_KEY:-${MYNAH_SUPABASE_SERVICE_ROLE_KEY:-}}"

if [[ -z "$SUPABASE_URL" || -z "$SERVICE_KEY" ]]; then
  echo "Need MYNAH_SUPABASE_URL and SUPABASE_SERVICE_ROLE_KEY (see $ENV_FILE)" >&2
  exit 1
fi

generate() {
  local slug="$1"
  local name="$2"
  echo "generate-busts: $slug ($name)"
  curl -sfS -X POST \
    -H "Authorization: Bearer $SERVICE_KEY" \
    -H "apikey: $SERVICE_KEY" \
    -H "Content-Type: application/json" \
    -d "{\"faculty_slug\":\"$slug\",\"name\":\"$name\"}" \
    "$SUPABASE_URL/functions/v1/generate-busts"
  echo
}

generate a.darwin "Charles Darwin"
generate a.einstein "Einstein"
generate nabokov "Vladimir Nabokov"
generate hesse "Hermann Hesse"
generate a.huxley "Aldous Huxley"
generate t.leary "Tim Leary"
generate m.shelley "Mary Shelley"
generate j.austen "Jane Austen"

echo "Done. Right 3/4 busts live at busts/{slug}/bust.png (not bust_frontal.png)."
echo "Verify: curl -I \"$SUPABASE_URL/functions/v1/faculty-bust?faculty=m.shelley&w=466&h=466&view=right\""
