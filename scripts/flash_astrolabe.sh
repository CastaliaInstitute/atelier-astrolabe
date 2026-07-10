#!/usr/bin/env bash
# DEPRECATED: legacy PlatformIO/Arduino firmware flasher.
# Use ./scripts/astrolabe175c_build.sh flash-core -p <port> for 1.75C hardware.
#
# Build Astrolabe firmware (embeds git commit message) and flash the watch.
# Commit before flashing so the daily briefing can speak what changed.
#
#   ./scripts/flash_astrolabe.sh
#   ./scripts/flash_astrolabe.sh --port /dev/cu.usbmodem1401
#   ./scripts/flash_astrolabe.sh --mac a4:cb:8f:d6:42:60 --env waveshare_s3_175_cameo
#   ./scripts/flash_astrolabe.sh --env waveshare_s3_175_ocarina
#   ./scripts/flash_astrolabe.sh --allow-dirty   # flash with uncommitted changes (not recommended)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

if [[ "${ASTROLABE_ALLOW_DEPRECATED_PLATFORMIO:-0}" != "1" ]]; then
  cat >&2 <<'EOF'
error: scripts/flash_astrolabe.sh is deprecated for Astrolabe hardware.

Use the ESP-IDF 1.75C firmware instead:
  ./scripts/astrolabe175c_identify.sh /dev/cu.usbmodemXXXX
  ./scripts/astrolabe175c_build.sh flash-core -p /dev/cu.usbmodemXXXX

If you intentionally need the legacy PlatformIO sketch, rerun with:
  ASTROLABE_ALLOW_DEPRECATED_PLATFORMIO=1 ./scripts/flash_astrolabe.sh ...
EOF
  exit 1
fi

ALLOW_DIRTY=0
PORT="${ASTROLABE_UPLOAD_PORT:-}"
MAC="${ASTROLABE_DEVICE_MAC:-}"
ENV="${PIO_ENV:-waveshare_s3_175}"
ENV_EXPLICIT=0
while [[ $# -gt 0 ]]; do
  case "$1" in
    --allow-dirty) ALLOW_DIRTY=1; shift ;;
    --env=*) ENV="${1#--env=}"; ENV_EXPLICIT=1; shift ;;
    --env) ENV="${2:-}"; ENV_EXPLICIT=1; shift 2 ;;
    --mac=*) MAC="${1#--mac=}"; shift ;;
    --mac) MAC="${2:-}"; shift 2 ;;
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

if [[ -n "$MAC" ]]; then
  if [[ -n "$PORT" ]]; then
    PORT="$(./scripts/resolve_esp_port_by_mac.sh "$MAC" "$PORT")"
  else
    PORT="$(./scripts/resolve_esp_port_by_mac.sh "$MAC")"
  fi
elif [[ -z "$PORT" ]]; then
  PORT="$(./scripts/detect_upload_port.sh)"
fi
if [[ "$ENV_EXPLICIT" -eq 0 && -z "${PIO_ENV:-}" && "$PORT" == *5A360268091 ]]; then
  echo "→ 1.75 native USB not found; using 1.85 fallback env"
  ENV="waveshare_s3_185_astrolabe"
fi

echo "→ HEAD: $(git -C "$ROOT" log -1 --oneline)"
[[ -n "$MAC" ]] && echo "→ target MAC: ${MAC}"
echo "→ build ${ENV} (generates sketches/Astrolabe/pm_build_info.h with commit message)"
PIO_ENV="$ENV" env -u PLATFORMIO_BUILD_DIR "$ROOT/scripts/build.sh"

UPLOAD_ARGS=(-e "$ENV" -t upload)
UPLOAD_ARGS+=(--upload-port "$PORT")
echo "→ upload ${PORT:-auto port}"
env -u PLATFORMIO_BUILD_DIR pio run "${UPLOAD_ARGS[@]}"

bash ./scripts/postupload-watchdog-reset.sh "$PORT" || true

echo ""
echo "After WiFi connects, the watch should auto-play the daily briefing"
echo "(includes a spoken summary of the commit above). Tap home to replay."
