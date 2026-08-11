#!/usr/bin/env bash
# Create GitHub issue for Faculty face (idempotent: skips if title already exists).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
REPO="${ASTROLABE_GH_REPO:-CastaliaInstitute/astrolabe}"
TITLE="[Feature] Faculty face — STT, ask-faculty, bust + TTS"
BODY_FILE="$ROOT/scripts/issue-bodies/faculty-face.md"

if [[ ! -f "$BODY_FILE" ]]; then
  echo "error: missing $BODY_FILE" >&2
  exit 1
fi

existing="$(gh issue list --repo "$REPO" --state open --search "in:title Faculty face STT" --json number,title --jq '.[0].number' 2>/dev/null || true)"
if [[ -n "$existing" && "$existing" != "null" ]]; then
  echo "Issue already exists: https://github.com/$REPO/issues/$existing"
  exit 0
fi

url="$(gh issue create --repo "$REPO" --title "$TITLE" --label enhancement --body-file "$BODY_FILE")"
num="${url##*/}"
echo "Created: $url"
echo ""
echo "Update docs/BACKLOG.md Faculty face line with: Issue: [#$num]($url)"
