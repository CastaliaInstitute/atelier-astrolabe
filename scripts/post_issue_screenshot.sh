#!/usr/bin/env bash
# Post a hardware QA screenshot to a GitHub issue (public gist embed + comment).
#
#   ./scripts/post_issue_screenshot.sh 2 artifacts/qa/moon.png "Moon face QA"
#
set -euo pipefail
ISSUE="${1:?issue number}"
IMAGE="${2:?image path (png)}"
TITLE="${3:-Hardware QA screenshot}"
REPO="${ASTROLABE_GH_REPO:-CastaliaInstitute/astrolabe}"

if [[ ! -f "$IMAGE" ]]; then
  echo "error: missing image ${IMAGE}" >&2
  exit 1
fi

NAME="astrolabe-qa-$(basename "$IMAGE")"
GIST_OUT="$(mktemp)"
gh gist create "$IMAGE" --public --desc "$TITLE" >"$GIST_OUT"
GIST_URL="$(tr -d '[:space:]' <"$GIST_OUT")"
rm -f "$GIST_OUT"

# Raw URL for markdown embed (gist create returns html URL)
GIST_ID="${GIST_URL##*/}"
RAW="$(gh api "gists/${GIST_ID}" --jq ".files | to_entries[0].value.raw_url")"

RUN_URL=""
if [[ -n "${GITHUB_SERVER_URL:-}" && -n "${GITHUB_REPOSITORY:-}" && -n "${GITHUB_RUN_ID:-}" ]]; then
  RUN_URL="${GITHUB_SERVER_URL}/${GITHUB_REPOSITORY}/actions/runs/${GITHUB_RUN_ID}"
fi

BODY_FILE="$(mktemp)"
{
  echo "## ${TITLE}"
  echo ""
  echo "![watch face](${RAW})"
  echo ""
  echo "- **Issue:** #${ISSUE}"
  echo "- **Image:** [gist](${GIST_URL})"
  if [[ -n "$RUN_URL" ]]; then
    echo "- **Workflow run:** ${RUN_URL}"
  fi
} >"$BODY_FILE"

gh issue comment "$ISSUE" --repo "$REPO" --body-file "$BODY_FILE"
rm -f "$BODY_FILE"
echo "→ commented on #${ISSUE} with ${RAW}"
