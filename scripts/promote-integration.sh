#!/usr/bin/env bash
# Promote integration → main after local build and on-device flash QA.
#
#   ./scripts/promote-integration.sh --flash-ok
#   FLASH_QA_OK=1 ./scripts/promote-integration.sh
#
# `main` should always build and have been flashed on hardware at promotion time.
# Issue PRs land on `integration` first; see docs/WORKFLOW.md.
#
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
REPO="${ASTROLABE_GH_REPO:-CastaliaInstitute/astrolabe}"
INTEGRATION_BRANCH="${INTEGRATION_BRANCH:-integration}"
MAIN_BRANCH="${MAIN_BRANCH:-main}"

usage() {
  cat <<EOF
Usage: $(basename "$0") [options]

Merge origin/${INTEGRATION_BRANCH} into ${MAIN_BRANCH} and push (promotion to release).

Options:
  --flash-ok          Required: attest you flashed integration on hardware and smoke-tested
  --skip-functional   Skip functional-test report check (not recommended)
  --dry-run           Show planned merge; do not push
  --skip-build        Skip ./scripts/build.sh (not recommended)
  -h, --help

Environment:
  FLASH_QA_OK=1       Same as --flash-ok
EOF
}

flash_ok=0
dry_run=0
skip_build=0
skip_functional=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --flash-ok) flash_ok=1; shift ;;
    --skip-functional) skip_functional=1; shift ;;
    --dry-run) dry_run=1; shift ;;
    --skip-build) skip_build=1; shift ;;
    -h | --help) usage; exit 0 ;;
    *) echo "unknown option: $1" >&2; usage >&2; exit 1 ;;
  esac
done

if [[ "${FLASH_QA_OK:-}" == "1" ]]; then
  flash_ok=1
fi

if [[ "$flash_ok" != "1" ]]; then
  echo "error: promotion requires on-device verification." >&2
  echo "  Flash origin/${INTEGRATION_BRANCH}, smoke-test, then re-run with --flash-ok" >&2
  exit 1
fi

if ! git diff --quiet || ! git diff --cached --quiet; then
  echo "error: working tree must be clean before promotion" >&2
  exit 1
fi

echo "→ fetch origin"
git fetch origin "$INTEGRATION_BRANCH" "$MAIN_BRANCH"

if [[ "$skip_functional" != "1" ]]; then
  FT_REPORT="${ASTROLABE_FT_HARDWARE_REPORT:-${ASTROLABE_FT_REPORT:-$ROOT/artifacts/functional/latest/report.json}}"
  if [[ ! -f "$FT_REPORT" ]]; then
    echo "error: missing hardware functional test report: ${FT_REPORT}" >&2
    echo "  Run Integration device gate on m1 or: ./scripts/functional_test.py --flash" >&2
    echo "  (Sim-only reports under artifacts/functional-sim/ do not qualify for main promotion.)" >&2
    echo "  Or promote with --skip-functional (not recommended)" >&2
    exit 1
  fi
  integr_sha_pre="$(git rev-parse "origin/${INTEGRATION_BRANCH}")"
  python3 -c "
import json, sys
from pathlib import Path
r = json.loads(Path('${FT_REPORT}').read_text())
gate = r.get('gate', 'hardware')
if gate != 'hardware':
    print(f'error: report gate={gate!r} — promotion requires hardware device gate', file=sys.stderr)
    sys.exit(1)
failed = int(r.get('failed', 0))
head = r.get('git_head', '')
want = '${integr_sha_pre}'
if failed:
    print(f'error: hardware functional test reported {failed} failed face(s)', file=sys.stderr)
    sys.exit(1)
if want and head and not (head.startswith(want[:8]) or want.startswith(head[:8])):
    print(f'error: report git_head {head[:12]} does not match integration {want[:12]}', file=sys.stderr)
    sys.exit(1)
print(f\"→ hardware functional test OK ({r.get('passed', 0)} faces) @ {head[:12] or 'unknown'}\")
" || exit 1
fi

if [[ "$skip_build" != "1" ]]; then
  echo "→ build at origin/${INTEGRATION_BRANCH}"
  wt="$(mktemp -d /tmp/astrolabe-promote-build.XXXXXX)"
  git worktree add "$wt" "origin/${INTEGRATION_BRANCH}"
  trap 'git worktree remove -f "$wt" 2>/dev/null || true' EXIT
  (cd "$wt" && ./scripts/build.sh)
  trap - EXIT
  git worktree remove -f "$wt"
fi

integr_sha="$(git rev-parse "origin/${INTEGRATION_BRANCH}")"
main_sha="$(git rev-parse "origin/${MAIN_BRANCH}")"

echo "→ integration: ${integr_sha:0:8}"
echo "→ main:        ${main_sha:0:8}"

if git merge-base --is-ancestor "$integr_sha" "$main_sha" 2>/dev/null; then
  if [[ "$integr_sha" == "$main_sha" ]]; then
    echo "nothing to promote (main already at integration)"
    exit 0
  fi
  echo "note: main is ahead of integration; promotion will still merge integration into main"
fi

if [[ "$dry_run" == "1" ]]; then
  echo "dry-run: would merge origin/${INTEGRATION_BRANCH} → ${MAIN_BRANCH} and push"
  exit 0
fi

prev_branch="$(git branch --show-current)"
git checkout "$MAIN_BRANCH"
git pull origin "$MAIN_BRANCH"

if ! git merge --no-ff "origin/${INTEGRATION_BRANCH}" -m "$(cat <<EOF
Promote ${INTEGRATION_BRANCH} to ${MAIN_BRANCH}.

Build verified locally; hardware device-gate report + flash QA attested via promote-integration.sh.
EOF
)"; then
  echo "error: merge failed — resolve conflicts, then push ${MAIN_BRANCH} manually" >&2
  exit 1
fi

git push origin "$MAIN_BRANCH"
git checkout "$prev_branch" 2>/dev/null || true

echo "✓ promoted origin/${INTEGRATION_BRANCH} → origin/${MAIN_BRANCH}"
