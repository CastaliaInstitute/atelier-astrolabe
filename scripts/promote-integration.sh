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
  --dry-run           Show planned merge; do not push
  --skip-build        Skip native astrolabe175c build (not recommended)
  -h, --help

Environment:
  FLASH_QA_OK=1       Same as --flash-ok
EOF
}

flash_ok=0
dry_run=0
skip_build=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --flash-ok) flash_ok=1; shift ;;
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

if [[ "$skip_build" != "1" ]]; then
  echo "→ build at origin/${INTEGRATION_BRANCH}"
  wt="$(mktemp -d /tmp/astrolabe-promote-build.XXXXXX)"
  git worktree add "$wt" "origin/${INTEGRATION_BRANCH}"
  trap 'git worktree remove -f "$wt" 2>/dev/null || true' EXIT
  (cd "$wt" && ./scripts/astrolabe175c_build.sh build)
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

Native ESP-IDF build verified locally; physical-device flash QA attested via promote-integration.sh.
EOF
)"; then
  echo "error: merge failed — resolve conflicts, then push ${MAIN_BRANCH} manually" >&2
  exit 1
fi

git push origin "$MAIN_BRANCH"
git checkout "$prev_branch" 2>/dev/null || true

echo "✓ promoted origin/${INTEGRATION_BRANCH} → origin/${MAIN_BRANCH}"
