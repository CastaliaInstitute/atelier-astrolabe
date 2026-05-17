#!/usr/bin/env bash
# Copy or generate include/secrets.local.h for self-hosted device workflows.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

DEST="${ROOT}/include/secrets.local.h"
if [[ -f "$DEST" ]]; then
  echo "→ secrets.local.h present"
  exit 0
fi

for src in \
  "${ASTROLABE_SECRETS_FILE:-}" \
  "$HOME/GitHub/astrolabe/include/secrets.local.h" \
  "$HOME/GitHub/CastaliaInstitute/astrolabe/include/secrets.local.h"; do
  if [[ -n "$src" && -f "$src" ]]; then
    cp "$src" "$DEST"
    echo "→ secrets from ${src}"
    exit 0
  fi
done

if [[ -n "${MYNAH_WIFI_SSID:-}" ]]; then
  "${ROOT}/scripts/write_secrets_ci.sh"
  exit 0
fi

echo "error: no secrets — set ASTROLABE_SECRETS_FILE, add include/secrets.local.h, or MYNAH_WIFI_*" >&2
exit 1
