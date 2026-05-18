#!/usr/bin/env bash
# Copy bootstrap/synastry-pwa into a local CastaliaInstitute/synastry clone and push.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
DEST="${1:-$HOME/GitHub/CastaliaInstitute/synastry}"
if [[ ! -d "$DEST/.git" ]]; then
  echo "Clone first: git clone https://github.com/CastaliaInstitute/synastry.git $DEST" >&2
  exit 1
fi
rsync -a --delete \
  --exclude push-to-synastry-repo.sh \
  --exclude deploy-github-pages.yml.example \
  --exclude README.md \
  "$ROOT/" "$DEST/docs/"
cp "$ROOT/README.md" "$DEST/README.md"
cp "$ROOT/deploy-github-pages.yml.example" "$DEST/.github/workflows/deploy-github-pages.yml"
cd "$DEST"
git checkout -B cursor/synastry-pwa-9e02
git add -A
git status
echo "Review, then: git commit && git push -u origin cursor/synastry-pwa-9e02"
