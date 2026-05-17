#!/usr/bin/env bash
# Revert integration commits that touch a clock face (isolate a failing face).
#
#   ./scripts/functional_unmerge_face.sh moon
#   ./scripts/functional_unmerge_face.sh moon --dry-run
#   ./scripts/functional_unmerge_face.sh moon --push   # push origin integration
#
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

FACE="${1:-}"
DRY_RUN=0
PUSH=0
INTEGRATION="${INTEGRATION_BRANCH:-integration}"
MAIN="${MAIN_BRANCH:-main}"

usage() {
  cat <<EOF
Usage: functional_unmerge_face.sh <face-name> [--dry-run] [--push]

Reverts commits on ${INTEGRATION} (since merge-base with ${MAIN}) that touch the face's
source_paths from tests/functional/faces_astrolabe.json.

Requires a clean working tree unless --dry-run.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --dry-run) DRY_RUN=1; shift ;;
    --push) PUSH=1; shift ;;
    -h | --help) usage; exit 0 ;;
    -*) echo "unknown option: $1" >&2; usage >&2; exit 1 ;;
    *)
      if [[ -z "$FACE" ]]; then
        FACE="$1"
      fi
      shift
      ;;
  esac
done

if [[ -z "$FACE" ]]; then
  usage >&2
  exit 1
fi

MATRIX="${ASTROLABE_FT_MATRIX:-$ROOT/tests/functional/faces_astrolabe.json}"
PATHS_JSON="$(python3 -c "
import json, sys
from pathlib import Path
m = json.loads(Path('${MATRIX}').read_text())
face = '${FACE}'.lower()
for f in m['faces']:
    if f['name'] == face or str(f['id']) == face:
        for p in f.get('source_paths', []):
            print(p)
        sys.exit(0)
print('error: unknown face', face, file=sys.stderr)
sys.exit(1)
")"

mapfile -t PATHS <<<"$PATHS_JSON"
if [[ ${#PATHS[@]} -eq 0 ]]; then
  echo "error: no source_paths for face ${FACE}" >&2
  exit 1
fi

git fetch origin "${MAIN}" "${INTEGRATION}" 2>/dev/null || true
BASE="$(git merge-base "origin/${MAIN}" "origin/${INTEGRATION}" 2>/dev/null || git merge-base "${MAIN}" "${INTEGRATION}")"
HEAD="$(git rev-parse "origin/${INTEGRATION}" 2>/dev/null || git rev-parse "${INTEGRATION}")"

echo "→ face ${FACE}"
echo "→ paths: ${PATHS[*]}"
echo "→ range ${BASE:0:12}..${HEAD:0:12} on ${INTEGRATION}"

mapfile -t SHAS < <(git log --reverse --format=%H "${BASE}".."${HEAD}" -- "${PATHS[@]}")
if [[ ${#SHAS[@]} -eq 0 ]]; then
  echo "→ no commits touch this face in range (nothing to revert)"
  exit 0
fi

echo "→ commits to revert (${#SHAS[@]}):"
for s in "${SHAS[@]}"; do
  git log -1 --oneline "$s"
done

if [[ "$DRY_RUN" == "1" ]]; then
  echo "→ dry-run: would revert ${#SHAS[@]} commit(s) on ${INTEGRATION}"
  exit 0
fi

if ! git diff --quiet || ! git diff --cached --quiet; then
  echo "error: working tree not clean — commit or stash first" >&2
  exit 1
fi

BRANCH="revert-ft-${FACE}-$(date +%Y%m%d%H%M%S)"
git checkout -B "$BRANCH" "origin/${INTEGRATION}" 2>/dev/null || git checkout -B "$BRANCH" "${INTEGRATION}"

for s in "${SHAS[@]}"; do
  echo "→ revert ${s:0:12}"
  if ! git revert --no-edit "$s"; then
    echo "error: revert failed (conflict?) — resolve manually on ${BRANCH}" >&2
    exit 1
  fi
done

echo "→ branch ${BRANCH} ready (${#SHAS[@]} revert(s))"
if [[ "$PUSH" == "1" ]]; then
  git push -u origin "$BRANCH"
  if command -v gh >/dev/null; then
    gh pr create --base "${INTEGRATION}" --head "$BRANCH" \
      --title "revert: isolate failing face ${FACE} (functional test)" \
      --body "$(cat <<EOF
## Summary
Functional test failed on clock face **\`${FACE}\`**. This PR reverts the commits that introduced or last changed that face on \`${INTEGRATION}\`, so \`integration\` stays green while a fix lands on a separate branch.

Re-run after merge: \`./scripts/functional_test.py --faces ${FACE}\`

Automated by \`functional_unmerge_face.sh\`.
EOF
)" || true
  fi
else
  echo "→ push: git push -u origin ${BRANCH} && open PR to ${INTEGRATION}"
fi
