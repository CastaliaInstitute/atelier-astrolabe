#!/usr/bin/env bash
# Launch a Cursor Cloud Agent for a GitHub issue (no pasting URLs in the browser).
#
#   export CURSOR_API_KEY="..."   # https://cursor.com/dashboard/integrations
#   ./scripts/cloud-agent.sh 2
#   ./scripts/cloud-agent.sh 2 --watch
#   ./scripts/cloud-agent.sh --agent bc-... --watch
#
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
REPO_SLUG="${ASTROLABE_GH_REPO:-CastaliaInstitute/astrolabe}"
REPO_URL="https://github.com/${REPO_SLUG}"
API_BASE="${CURSOR_API_BASE:-https://api.cursor.com}"

usage() {
  cat <<'EOF'
Usage:
  cloud-agent.sh <issue#> [options]     Start cloud agent from GitHub issue
  cloud-agent.sh --agent <id> [opts]  Watch/status an existing agent

Options:
  --branch <name>     Git branch (default: fix|feature/<#>-<slug> from title)
  --watch             Stream run events (SSE) after launch
  --status            Poll run status until terminal (with --agent only)
  --dry-run           Print JSON payload; do not call API
  --no-pr             Do not open a PR when the run finishes
  -h, --help

Requires: gh, curl, jq, CURSOR_API_KEY (or .env / scripts/cloud-agent.env.local)
EOF
}

load_api_key() {
  if [[ -n "${CURSOR_API_KEY:-}" ]]; then
    return 0
  fi
  local f
  for f in "$ROOT/scripts/cloud-agent.env.local" "$ROOT/.env" "$ROOT/.env.local"; do
    if [[ -f "$f" ]]; then
      # shellcheck disable=SC1090
      set -a
      source "$f"
      set +a
      if [[ -n "${CURSOR_API_KEY:-}" ]]; then
        return 0
      fi
    fi
  done
  local castalia="${CASTALIA_ENV:-$HOME/GitHub/CastaliaInstitute/castalia.institute/.env}"
  if [[ -f "$castalia" ]]; then
    local line
    line="$(grep -E '^CURSOR_API_KEY=' "$castalia" 2>/dev/null | tail -1 || true)"
    if [[ -n "$line" ]]; then
      export "${line?}"
      return 0
    fi
  fi
  echo "error: set CURSOR_API_KEY or create scripts/cloud-agent.env.local" >&2
  exit 1
}

slugify() {
  echo "$1" | tr '[:upper:]' '[:lower:]' | sed -E 's/^\[(bug|feature)\][[:space:]]*//i' |
    tr -cs 'a-z0-9' '-' | sed -E 's/^-+|-+$//g' | cut -c1-48
}

default_branch_for_issue() {
  local num="$1" title="$2" labels_json="$3"
  local kind="feature"
  if echo "$title" | grep -qiE '^\[bug\]'; then
    kind="fix"
  elif echo "$labels_json" | jq -e '.[] | select(.name=="bug")' >/dev/null 2>&1; then
    kind="fix"
  fi
  local slug
  slug="$(slugify "$title")"
  echo "${kind}/${num}-${slug}"
}

build_prompt() {
  local num="$1" title="$2" body="$3" branch="$4"
  cat <<EOF
Implement GitHub issue #${num} for ${REPO_SLUG}: ${title}

Branch: ${branch} (create and push this branch)

Read:
- Issue body (below)
- docs/BACKLOG.md, docs/WORKFLOW.md
- .cursor/rules/backlog.mdc, git-workflow.mdc, github-workflow.mdc
- hardware-qa.mdc for clock face / UI changes

Deliverables:
- Code changes; ./scripts/astrolabe175c_build.sh build must pass
- Update docs/BACKLOG.md (In progress → Done with PR link)
- Open a PR against integration with "Closes #${num}"
- When required native ESP-IDF checks are green and scope is only this issue: push the branch and open or merge the PR into integration according to repository policy
- Do not merge to main (promotion is ./scripts/promote-integration.sh --flash-ok after hardware device gate / flash QA)
- Do not commit secrets

Hardware note: cloud cannot flash the watch. Record that physical-device QA is still required for hardware-facing changes and before promotion to main.

--- issue body ---
${body}
--- end issue ---
EOF
}

api_curl() {
  curl -sS -u "${CURSOR_API_KEY}:" "$@"
}

# Set by start_agent on success
AGENT_ID=""
RUN_ID=""
AGENT_URL=""

start_agent() {
  local issue_num="$1" branch="$2" dry_run="$3" auto_pr="$4"
  local title body labels_json
  title="$(gh issue view "$issue_num" --repo "$REPO_SLUG" --json title -q .title)"
  body="$(gh issue view "$issue_num" --repo "$REPO_SLUG" --json body -q .body)"
  labels_json="$(gh issue view "$issue_num" --repo "$REPO_SLUG" --json labels -q .labels)"
  if [[ -z "$branch" ]]; then
    branch="$(default_branch_for_issue "$issue_num" "$title" "$labels_json")"
  fi
  local prompt
  prompt="$(build_prompt "$issue_num" "$title" "$body" "$branch")"
  local payload
  # branchName in OpenAPI but not yet accepted by api.cursor.com — branch is in prompt text only
  payload="$(jq -n \
    --arg text "$prompt" \
    --arg url "$REPO_URL" \
    --argjson auto_pr "$auto_pr" \
    '{
      prompt: {text: $text},
      repos: [{url: $url, startingRef: "integration"}],
      autoCreatePR: $auto_pr,
      skipReviewerRequest: true
    }')"
  if [[ "$dry_run" == "1" ]]; then
    echo "$payload" | jq .
    return 0
  fi
  echo "→ issue #${issue_num}: ${title}" >&2
  echo "→ branch: ${branch}" >&2
  local resp
  resp="$(api_curl -X POST "${API_BASE}/v1/agents" \
    -H 'Content-Type: application/json' \
    -d "$payload")"
  if ! echo "$resp" | jq -e '.agent.id' >/dev/null 2>&1; then
    echo "error: API response:" >&2
    echo "$resp" | jq . >&2 || echo "$resp" >&2
    exit 1
  fi
  AGENT_ID="$(echo "$resp" | jq -r '.agent.id')"
  AGENT_URL="$(echo "$resp" | jq -r '.agent.url')"
  RUN_ID="$(echo "$resp" | jq -r '.run.id')"
  echo "agent:  ${AGENT_ID}"
  echo "run:    ${RUN_ID}"
  echo "url:    ${AGENT_URL}"
  echo ""
  echo "Watch:  $0 --agent ${AGENT_ID} --watch"
  echo "Status: $0 --agent ${AGENT_ID} --status"
}

watch_run() {
  local agent_id="$1" run_id="${2:-}"
  if [[ -z "$run_id" || "$run_id" == "null" ]]; then
    run_id="$(api_curl "${API_BASE}/v1/agents/${agent_id}" | jq -r '.latestRunId')"
  fi
  if [[ -z "$run_id" || "$run_id" == "null" ]]; then
    echo "error: no run id for agent ${agent_id}" >&2
    exit 1
  fi
  echo "streaming ${agent_id} / ${run_id} (Ctrl-C to stop)" >&2
  api_curl -N "${API_BASE}/v1/agents/${agent_id}/runs/${run_id}/stream" \
    -H 'Accept: text/event-stream' || true
}

poll_status() {
  local agent_id="$1" run_id="${2:-}"
  if [[ -z "$run_id" || "$run_id" == "null" ]]; then
    run_id="$(api_curl "${API_BASE}/v1/agents/${agent_id}" | jq -r '.latestRunId')"
  fi
  while true; do
    local st
    st="$(api_curl "${API_BASE}/v1/agents/${agent_id}/runs/${run_id}" | jq -r '.status')"
    echo "$(date -u +%H:%M:%S)  ${st}"
    case "$st" in
      FINISHED | FAILED | CANCELLED | ERROR)
        break
        ;;
    esac
    sleep 15
  done
}

main() {
  local issue_num="" agent_id="" branch="" dry_run=0 auto_pr=true
  local do_watch=0 do_status=0

  while [[ $# -gt 0 ]]; do
    case "$1" in
      -h | --help)
        usage
        exit 0
        ;;
      --branch)
        branch="$2"
        shift 2
        ;;
      --dry-run)
        dry_run=1
        shift
        ;;
      --watch)
        do_watch=1
        shift
        ;;
      --status)
        do_status=1
        shift
        ;;
      --no-pr)
        auto_pr=false
        shift
        ;;
      --agent)
        agent_id="$2"
        shift 2
        ;;
      [0-9]*)
        issue_num="$1"
        shift
        ;;
      *)
        echo "unknown argument: $1" >&2
        usage >&2
        exit 1
        ;;
    esac
  done

  command -v gh >/dev/null || {
    echo "error: gh CLI required" >&2
    exit 1
  }
  command -v jq >/dev/null || {
    echo "error: jq required" >&2
    exit 1
  }
  command -v curl >/dev/null || {
    echo "error: curl required" >&2
    exit 1
  }

  if [[ -n "$agent_id" ]]; then
    load_api_key
    if [[ "$do_watch" == "1" ]]; then
      watch_run "$agent_id"
      exit 0
    fi
    if [[ "$do_status" == "1" ]]; then
      poll_status "$agent_id"
      exit 0
    fi
    api_curl "${API_BASE}/v1/agents/${agent_id}" | jq .
    exit 0
  fi

  if [[ -z "$issue_num" ]]; then
    usage >&2
    exit 1
  fi

  if [[ "$dry_run" != "1" ]]; then
    load_api_key
  fi

  start_agent "$issue_num" "$branch" "$dry_run" "$auto_pr"
  if [[ "$dry_run" == "1" ]]; then
    exit 0
  fi
  if [[ "$do_watch" == "1" ]]; then
    watch_run "$AGENT_ID" "$RUN_ID"
  elif [[ "$do_status" == "1" ]]; then
    poll_status "$AGENT_ID" "$RUN_ID"
  fi
}

main "$@"
