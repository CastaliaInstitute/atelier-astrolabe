#!/usr/bin/env bash
# Guess ASTROLABE_QA_FACE from a GitHub issue title/body (moon, astro, spotify, …).
#
#   ./scripts/resolve_qa_face.sh 1
#   FACE=$(./scripts/resolve_qa_face.sh 1) && echo "$FACE"
#
set -euo pipefail
ISSUE="${1:?issue number}"
REPO="${ASTROLABE_GH_REPO:-CastaliaInstitute/astrolabe}"

JSON="$(gh issue view "$ISSUE" --repo "$REPO" --json title,body 2>/dev/null || true)"
if [[ -z "$JSON" ]]; then
  echo "moon"
  exit 0
fi

TEXT="$(printf '%s' "$JSON" | python3 -c 'import json,sys; d=json.load(sys.stdin); print((d.get("title") or "")+"\n"+(d.get("body") or ""))' | tr '[:upper:]' '[:lower:]')"

pick() {
  local name="$1"
  if printf '%s' "$TEXT" | grep -qE "(^|[^a-z])${name}([^a-z]|$)"; then
    echo "$name"
    exit 0
  fi
}

for name in synastry syn moon hafez castalia calcifer schedule spotify astro astrology apocalypso digital classic analog hue; do
  pick "$name"
done

echo "moon"
