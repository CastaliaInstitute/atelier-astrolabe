#!/usr/bin/env bash
# Push CURSOR_API_KEY from local env to GitHub Actions (one-time / rotate).
#
#   ./scripts/sync-cursor-github-secret.sh
#
# Reads (first match): scripts/cloud-agent.env.local, .env, .env.local,
# or ~/GitHub/CastaliaInstitute/castalia.institute/.env
#
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
REPO="${ASTROLABE_GH_REPO:-CastaliaInstitute/astrolabe}"

load_key() {
  if [[ -n "${CURSOR_API_KEY:-}" ]]; then
    return 0
  fi
  local f castalia
  for f in "$ROOT/scripts/cloud-agent.env.local" "$ROOT/.env" "$ROOT/.env.local"; do
    if [[ -f "$f" ]]; then
      set -a
      # shellcheck disable=SC1090
      source "$f"
      set +a
      if [[ -n "${CURSOR_API_KEY:-}" ]]; then
        echo "→ from ${f}" >&2
        return 0
      fi
    fi
  done
  castalia="${CASTALIA_ENV:-$HOME/GitHub/CastaliaInstitute/castalia.institute/.env}"
  if [[ -f "$castalia" ]]; then
    local line
    line="$(grep -E '^CURSOR_API_KEY=' "$castalia" | tail -1 || true)"
    if [[ -n "$line" ]]; then
      export "${line?}"
      echo "→ from ${castalia}" >&2
      return 0
    fi
  fi
  echo "error: CURSOR_API_KEY not in cloud-agent.env.local, .env, or castalia .env" >&2
  exit 1
}

load_key
command -v gh >/dev/null || {
  echo "error: gh CLI required" >&2
  exit 1
}

gh secret set CURSOR_API_KEY --repo "$REPO" --body "$CURSOR_API_KEY"
echo "✓ GitHub Actions secret CURSOR_API_KEY updated for ${REPO}"
