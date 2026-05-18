#!/usr/bin/env bash
# Create CastaliaInstitute/synastra on GitHub and push this directory.
# Run locally with gh authenticated as an org admin:
#   ./scripts/create-github-repo.sh

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

if ! command -v gh >/dev/null; then
  echo "error: install GitHub CLI (gh)" >&2
  exit 1
fi

if [[ -d .git ]]; then
  echo "error: run from a clean export without .git (or delete .git first)" >&2
  exit 1
fi

git init -b main
git add -A
git commit -m "Initial Synastra landing site with GitHub Pages and Cloudflare DNS tooling"

gh repo create CastaliaInstitute/synastra \
  --public \
  --description "Synastra — relational awareness for the home (synastra.castalia.institute)" \
  --source . \
  --remote origin \
  --push

gh api -X POST "repos/CastaliaInstitute/synastra/pages" -f build_type=workflow || true
gh api -X PUT "repos/CastaliaInstitute/synastra/pages" \
  -f cname='synastra.castalia.institute' \
  -f https_enforced=true || true

echo "Enable Pages: Settings → Pages → Source = GitHub Actions (if not already)."
echo "DNS: export CLOUDFLARE_API_TOKEN=... && ./scripts/cf-dns-synastra-github-pages.sh"
echo "Or: gh workflow run sync-synastra-dns.yml -R CastaliaInstitute/synastra"
