#!/usr/bin/env bash
# Clone the Mynah Atom mesh peer repo (sibling to astrolabe by default).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
REPO_SLUG="${ATOM_GH_REPO:-CastaliaInstitute/atom}"
DEST="${ATOM_DIR:-${ROOT}/atom}"

if [[ -d "$DEST/.git" ]]; then
  echo "atom: already cloned at $DEST"
  (cd "$DEST" && git fetch origin && git pull --ff-only origin "$(git symbolic-ref --short HEAD 2>/dev/null || echo main)" 2>/dev/null || true)
  exit 0
fi

if [[ -d "$DEST" ]]; then
  echo "error: $DEST exists but is not a git repo" >&2
  exit 1
fi

echo "atom: cloning https://github.com/${REPO_SLUG} -> $DEST"
gh repo clone "$REPO_SLUG" "$DEST"
