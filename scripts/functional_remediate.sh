#!/usr/bin/env bash
# On functional-test failure: triage → GitHub issue → unmerge face off integration → optional fix agent.
#
#   ./scripts/functional_remediate.sh artifacts/functional/20260517-120000/report.json
#
# Env:
#   ASTROLABE_FT_UNMERGE=1          Revert face commits (default 1)
#   ASTROLABE_FT_DISPATCH_AGENT=1     Start Cursor cloud agent (needs CURSOR_API_KEY)
#   ASTROLABE_FT_UNMERGE_PUSH=1     Push revert branch + open PR (needs gh auth)
#
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

REPORT="${1:-}"
if [[ -z "$REPORT" || ! -f "$REPORT" ]]; then
  echo "usage: functional_remediate.sh <artifacts/functional/.../report.json>" >&2
  exit 1
fi

OUT_DIR="$(dirname "$REPORT")"
REPO_SLUG="${ASTROLABE_GH_REPO:-CastaliaInstitute/astrolabe}"
UNMERGE="${ASTROLABE_FT_UNMERGE:-1}"
AGENT="${ASTROLABE_FT_DISPATCH_AGENT:-0}"
UNMERGE_PUSH="${ASTROLABE_FT_UNMERGE_PUSH:-0}"

INTEGRATION_REF="$(git rev-parse HEAD 2>/dev/null || echo HEAD)"
python3 "${ROOT}/scripts/functional_triage.py" \
  --report "$REPORT" \
  --integration-ref "$INTEGRATION_REF"

FAILED="$(python3 -c "
import json
from pathlib import Path
r = json.loads(Path('$REPORT').read_text())
print(','.join(f['name'] for f in r['faces'] if not f['ok']))
")"

if [[ -z "$FAILED" ]]; then
  echo "→ no failed faces"
  exit 0
fi

IFS=',' read -ra FACES <<<"$FAILED"
for face in "${FACES[@]}"; do
  TRIAGE="${OUT_DIR}/"*"${face}"*-triage.md
  TRIAGE_FILE="$(ls -1 ${TRIAGE} 2>/dev/null | head -1 || true)"
  if [[ -z "$TRIAGE_FILE" ]]; then
    # id-prefixed: 05-moon-triage.md
    TRIAGE_FILE="$(find "$OUT_DIR" -name "*-${face}-triage.md" | head -1)"
  fi

  TITLE="[FT] ${face} face crashes on device (functional test)"
  BODY="$(cat <<EOF
Functional test failed on clock face **\`${face}\`** on \`${INTEGRATION_REF:0:12}\`.

## Triage
$(cat "$TRIAGE_FILE" 2>/dev/null || echo "_triage file missing_")

## Actions taken
- [ ] Face isolated from \`integration\` via revert PR
- [ ] Fix branch + functional test \`./scripts/functional_test.py --faces ${face}\`
- [ ] Re-merge to \`integration\` when green

Artifacts: \`${OUT_DIR}\`
EOF
)"

  LABELS="bug,face-${face},functional-test,needs-hardware-qa"
  if command -v gh >/dev/null; then
    EXISTING="$(gh issue list --repo "$REPO_SLUG" --state open --label "face-${face}" --label functional-test \
      --json number,title -q '.[0].number' 2>/dev/null || true)"
    if [[ -n "$EXISTING" && "$EXISTING" != "null" ]]; then
      echo "→ updating issue #${EXISTING} (open functional-test for ${face})"
      gh issue comment "$EXISTING" --repo "$REPO_SLUG" --body "$BODY" >/dev/null
      ISSUE_NUM="$EXISTING"
    else
      ISSUE_URL="$(gh issue create --repo "$REPO_SLUG" --title "$TITLE" --body "$BODY" --label "$LABELS" 2>/dev/null || \
        gh issue create --repo "$REPO_SLUG" --title "$TITLE" --body "$BODY")"
      echo "→ issue ${ISSUE_URL}"
      ISSUE_NUM="${ISSUE_URL##*/}"
    fi
  else
    echo "→ (no gh) issue title: ${TITLE}"
    ISSUE_NUM=""
  fi

  if [[ "$UNMERGE" == "1" ]]; then
    if [[ "$UNMERGE_PUSH" == "1" ]]; then
      "${ROOT}/scripts/functional_unmerge_face.sh" "$face" --push || true
    else
      "${ROOT}/scripts/functional_unmerge_face.sh" "$face" --dry-run
      echo "→ set ASTROLABE_FT_UNMERGE_PUSH=1 to open revert PR on integration"
    fi
  fi

  if [[ "$AGENT" == "1" && -n "$TRIAGE_FILE" ]]; then
    "${ROOT}/scripts/cloud-agent-fix-crash.sh" "$face" "$TRIAGE_FILE" || echo "→ agent dispatch failed (check CURSOR_API_KEY)"
  fi
done

echo "→ remediation complete for: ${FAILED}"
