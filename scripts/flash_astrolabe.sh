#!/usr/bin/env bash
# Build Astrolabe firmware (embeds git commit message) and flash the watch.
# Commit before flashing so the daily briefing can speak what changed.
#
#   ./scripts/flash_astrolabe.sh
#   ./scripts/flash_astrolabe.sh --port /dev/cu.usbmodem1401
#   ./scripts/flash_astrolabe.sh --allow-dirty   # flash with uncommitted changes (not recommended)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

ALLOW_DIRTY=0
PORT="${ASTROLABE_UPLOAD_PORT:-}"
while [[ $# -gt 0 ]]; do
  case "$1" in
    --allow-dirty) ALLOW_DIRTY=1; shift ;;
    --port=*) PORT="${1#--port=}"; shift ;;
    --port) PORT="${2:-}"; shift 2 ;;
    *) shift ;;
  esac
done

if ! git -C "$ROOT" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
  echo "error: not a git repository" >&2
  exit 1
fi

if [[ -n "$(git -C "$ROOT" status --porcelain)" ]]; then
  if [[ "$ALLOW_DIRTY" -eq 0 ]]; then
    echo "Working tree has uncommitted changes."
    echo "Commit first so the post-flash briefing uses your commit message:"
    echo "  git add -A && git commit -m \"Your change summary\""
    echo "Or pass --allow-dirty to flash anyway (briefing will note dirty build)."
    exit 1
  fi
  echo "warning: flashing with dirty tree — PM_BUILD_DIRTY=1 in firmware"
fi

echo "→ HEAD: $(git -C "$ROOT" log -1 --oneline)"
echo "→ build (generates sketches/Astrolabe/pm_build_info.h with commit message)"
env -u PLATFORMIO_BUILD_DIR "$ROOT/scripts/build.sh"

UPLOAD_ARGS=(-e waveshare_s3_175 -t upload)
if [[ -n "$PORT" ]]; then
  UPLOAD_ARGS+=(--upload-port "$PORT")
fi
echo "→ upload ${PORT:-auto port}"
env -u PLATFORMIO_BUILD_DIR pio run "${UPLOAD_ARGS[@]}"

if [[ -n "$PORT" ]]; then
  bash ./scripts/postupload-watchdog-reset.sh "$PORT" || true
fi

echo ""
echo "After WiFi connects, the watch should auto-play the daily briefing"
echo "(includes a spoken summary of the commit above). Tap home to replay."
