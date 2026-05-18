#!/usr/bin/env bash
# Dispatch Cursor Cloud Agent to fix a functional-test crash for one clock face.
#
#   ./scripts/cloud-agent-fix-crash.sh moon artifacts/functional/2026.../05-moon-triage.md
#   ./scripts/cloud-agent-fix-crash.sh --issue 42
#
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
REPO_SLUG="${ASTROLABE_GH_REPO:-CastaliaInstitute/astrolabe}"
API_BASE="${CURSOR_API_BASE:-https://api.cursor.com}"

FACE=""
TRIAGE_FILE=""
ISSUE_NUM=""
BRANCH=""

usage() {
  cat <<'EOF'
Usage:
  cloud-agent-fix-crash.sh <face> <triage.md>
  cloud-agent-fix-crash.sh --issue <N>   # uses issue body + labels for face name

Requires CURSOR_API_KEY (see scripts/cloud-agent.sh).
EOF
}

load_api_key() {
  if [[ -n "${CURSOR_API_KEY:-}" ]]; then
    return 0
  fi
  for f in "$ROOT/scripts/cloud-agent.env.local" "$ROOT/.env" "$ROOT/.env.local"; do
    if [[ -f "$f" ]]; then
      set -a
      # shellcheck source=/dev/null
      source "$f"
      set +a
      [[ -n "${CURSOR_API_KEY:-}" ]] && return 0
    fi
  done
  echo "error: CURSOR_API_KEY" >&2
  exit 1
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --issue) ISSUE_NUM="$2"; shift 2 ;;
    --branch) BRANCH="$2"; shift 2 ;;
    -h | --help) usage; exit 0 ;;
    *)
      if [[ -z "$FACE" ]]; then
        FACE="$1"
      elif [[ -z "$TRIAGE_FILE" ]]; then
        TRIAGE_FILE="$2"
      fi
      shift
      ;;
  esac
done

if [[ -n "$ISSUE_NUM" ]]; then
  FACE="${FACE:-$(gh issue view "$ISSUE_NUM" --repo "$REPO_SLUG" --json labels -q '.labels[].name' | grep -E '^face:' | head -1 | sed 's/face://')}"
  TRIAGE_FILE="${TRIAGE_FILE:-$(gh issue view "$ISSUE_NUM" --repo "$REPO_SLUG" --json body -q .body | grep -m1 'triage.md' | sed -E 's/.*(artifacts\/functional\/[^ )]+-triage.md).*/\1/' || true)}"
fi

[[ -n "$FACE" && -f "$TRIAGE_FILE" ]] || {
  usage >&2
  exit 1
}

BRANCH="${BRANCH:-fix/ft-${FACE}-crash}"
TRIAGE_BODY="$(cat "$TRIAGE_FILE")"

load_api_key

PROMPT="$(cat <<EOF
Fix a **functional-test crash** on clock face \`${FACE}\` in ${REPO_SLUG}.

## Context
- Branch from **integration** (face was reverted off integration; restore a correct fix).
- Triage report:

${TRIAGE_BODY}

## Requirements
1. Find root cause (null deref, stack, WiFi callback on wrong task, etc.) in the paths listed above.
2. Fix with minimal scope — only this face and shared code if required.
3. \`./scripts/build.sh\` must pass.
4. Open PR to **integration** titled \`fix(ft): ${FACE} functional test crash\`.
5. PR body must note: re-run \`./scripts/functional_test.py --faces ${FACE}\` on hardware before merge.
6. Do **not** merge until functional test passes (human or CI on astrolabe-watch runner).
7. No secrets in commits.

If the crash is in test injection (\`qa inject\`) only, fix the handler; if in real touch path, fix production code.
EOF
)"

PAYLOAD="$(jq -n \
  --arg text "$PROMPT" \
  --arg url "https://github.com/${REPO_SLUG}" \
  '{
    prompt: {text: $text},
    repos: [{url: $url, startingRef: "integration"}],
    autoCreatePR: true,
    skipReviewerRequest: true
  }')"

echo "→ face ${FACE} branch ${BRANCH}"
RESP="$(curl -sS -u "${CURSOR_API_KEY}:" -X POST "${API_BASE}/v1/agents" \
  -H 'Content-Type: application/json' \
  -d "$PAYLOAD")"
echo "$RESP" | jq -r '"agent: \(.agent.id)\nurl: \(.agent.url)"' 2>/dev/null || echo "$RESP"
