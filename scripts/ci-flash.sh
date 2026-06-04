#!/usr/bin/env bash
# Build and flash firmware on a machine with the watch on USB (CI self-hosted runner or local).
#
#   ./scripts/ci-flash.sh
#   ASTROLABE_DEVICE_MAC=a4:cb:8f:d6:42:60 PIO_ENV=waveshare_s3_175_cameo ./scripts/ci-flash.sh
#   ASTROLABE_UPLOAD_PORT=/dev/cu.usbmodem1101 ./scripts/ci-flash.sh
#   ./scripts/ci-flash.sh --upload-only   # skip rebuild if firmware.bin exists
#
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

UPLOAD_ONLY=0
SMOKE_SEC="${ASTROLABE_FLASH_SMOKE_SEC:-8}"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --upload-only) UPLOAD_ONLY=1; shift ;;
    --mac=*) export ASTROLABE_DEVICE_MAC="${1#--mac=}"; shift ;;
    --mac) export ASTROLABE_DEVICE_MAC="${2:-}"; shift 2 ;;
    -h | --help)
      echo "Usage: ci-flash.sh [--upload-only] [--mac MAC]"
      exit 0
      ;;
    *) echo "unknown option: $1" >&2; exit 1 ;;
  esac
done

ENV="${PIO_ENV:-waveshare_s3_175}"
BUILD_DIR="${PLATFORMIO_BUILD_DIR:-/tmp/astrolabe-pio-build}"
export PLATFORMIO_BUILD_DIR="$BUILD_DIR"

# USB port can re-enumerate during long compiles; optional hub VBUS cycle before upload.
bash ./scripts/usb-power-cycle-watch.sh || true
PORT="$(./scripts/detect_upload_port.sh)"
export ASTROLABE_UPLOAD_PORT="$PORT"
if [[ -z "${PIO_ENV:-}" && "$PORT" == *5A360268091 ]]; then
  echo "→ 1.75 native USB not found; using 1.85 fallback env"
  ENV="waveshare_s3_185_astrolabe"
fi
BIN="${BUILD_DIR}/${ENV}/firmware.bin"

if [[ "$UPLOAD_ONLY" != "1" ]] || [[ ! -f "$BIN" ]]; then
  echo "→ build ${ENV}"
  PIO_ENV="$ENV" ./scripts/build.sh
fi

if [[ ! -f "$BIN" ]]; then
  echo "error: missing ${BIN} after build" >&2
  exit 1
fi

echo "→ upload port: ${PORT}"
if [[ -n "${ASTROLABE_DEVICE_MAC:-}" ]]; then
  echo "→ upload MAC:  ${ASTROLABE_DEVICE_MAC}"
fi
echo "→ upload ${BIN}"

upload_once() {
  pio run -e "$ENV" -t upload --upload-port "$PORT" -j 1 "$@"
}

preupload_reset() {
  bash ./scripts/preupload-esptool-reset.sh "$PORT" 2>/dev/null || true
  bash ./scripts/preupload-boot-pulse.sh "$PORT" 2>/dev/null || true
}

TRIES="${ASTROLABE_UPLOAD_TRIES:-3}"
ok=0
for ((i = 1; i <= TRIES; i++)); do
  echo "→ upload attempt ${i}/${TRIES}"
  [[ "$i" -gt 1 ]] && bash ./scripts/usb-power-cycle-watch.sh || true
  PORT="$(./scripts/detect_upload_port.sh)"
  export ASTROLABE_UPLOAD_PORT="$PORT"
  if [[ -n "${ASTROLABE_DEVICE_MAC:-}" ]]; then
    echo "→ resolved ${ASTROLABE_DEVICE_MAC} to ${PORT}"
  fi
  preupload_reset
  upload_once && ok=1 && break
  sleep 2
done
if [[ "$ok" != "1" ]]; then
  echo "error: upload failed after ${TRIES} attempts" >&2
  echo "  Try: hold BOOT, tap PWR during upload, or check ASTROLABE_UHUBCTL_* hub settings" >&2
  exit 1
fi

bash ./scripts/postupload-watchdog-reset.sh "$PORT" || true

if [[ "$SMOKE_SEC" -gt 0 ]] && command -v timeout >/dev/null 2>&1; then
  echo "→ serial smoke (${SMOKE_SEC}s)"
  timeout "${SMOKE_SEC}" pio device monitor -e "$ENV" --port "$PORT" --baud 115200 --echo || true
elif [[ "$SMOKE_SEC" -gt 0 ]]; then
  echo "→ serial smoke skipped (no timeout command)"
fi

echo "✓ flash complete"
