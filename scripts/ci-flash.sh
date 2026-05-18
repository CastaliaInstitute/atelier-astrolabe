#!/usr/bin/env bash
# Build and flash firmware on a machine with the watch on USB (CI self-hosted runner or local).
#
#   ./scripts/ci-flash.sh
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
    -h | --help)
      echo "Usage: ci-flash.sh [--upload-only]"
      exit 0
      ;;
    *) echo "unknown option: $1" >&2; exit 1 ;;
  esac
done

ENV="${PIO_ENV:-waveshare_s3_175}"
BUILD_DIR="${PLATFORMIO_BUILD_DIR:-/tmp/astrolabe-pio-build}"
BIN="${BUILD_DIR}/${ENV}/firmware.bin"

if [[ "$UPLOAD_ONLY" != "1" ]] || [[ ! -f "$BIN" ]]; then
  echo "→ build ${ENV}"
  ./scripts/build.sh
fi

if [[ ! -f "$BIN" ]]; then
  echo "error: missing ${BIN} after build" >&2
  exit 1
fi

# Detect after build — USB port can re-enumerate during long compiles.
PORT="$(./scripts/detect_upload_port.sh)"
export ASTROLABE_UPLOAD_PORT="$PORT"
echo "→ upload port: ${PORT}"
echo "→ upload ${BIN}"
echo "→ if upload fails: hold BOOT, tap PWR (or plug USB), release BOOT when esptool connects"

upload_once() {
  pio run -e "$ENV" -t upload --upload-port "$PORT" -j 1 "$@"
}

if command -v "${ASTROLABE_CI_VENV:-$HOME/.astrolabe-ci-venv}/bin/python" >/dev/null 2>&1; then
  bash ./scripts/preupload-boot-pulse.sh "$PORT" 2>/dev/null || true
fi

TRIES="${ASTROLABE_UPLOAD_TRIES:-3}"
ok=0
for ((i = 1; i <= TRIES; i++)); do
  echo "→ upload attempt ${i}/${TRIES}"
  [[ "$i" -gt 1 ]] && { bash ./scripts/preupload-boot-pulse.sh "$PORT" 2>/dev/null || true; sleep 2; }
  if [[ "$i" -eq 1 ]]; then
    upload_once && ok=1 && break
  elif upload_once --upload-flags "--before=usb_reset" --upload-flags "--after=hard_reset"; then
    ok=1 && break
  fi
done
if [[ "$ok" != "1" ]]; then
  echo "error: upload failed after ${TRIES} attempts — put watch in download mode (hold BOOT, tap PWR)" >&2
  exit 1
fi

if [[ "$SMOKE_SEC" -gt 0 ]] && command -v timeout >/dev/null 2>&1; then
  echo "→ serial smoke (${SMOKE_SEC}s)"
  timeout "${SMOKE_SEC}" pio device monitor -e "$ENV" --port "$PORT" --baud 115200 --echo || true
elif [[ "$SMOKE_SEC" -gt 0 ]]; then
  echo "→ serial smoke skipped (no timeout command)"
fi

echo "✓ flash complete"
