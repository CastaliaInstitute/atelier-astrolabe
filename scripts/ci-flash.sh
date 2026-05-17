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

PORT="$(./scripts/detect_upload_port.sh)"
echo "→ upload port: ${PORT}"

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

echo "→ upload ${BIN}"
pio run -e "$ENV" -t upload --upload-port "$PORT" -j 1

if [[ "$SMOKE_SEC" -gt 0 ]] && command -v timeout >/dev/null 2>&1; then
  echo "→ serial smoke (${SMOKE_SEC}s)"
  timeout "${SMOKE_SEC}" pio device monitor -e "$ENV" --port "$PORT" --baud 115200 --echo || true
elif [[ "$SMOKE_SEC" -gt 0 ]]; then
  echo "→ serial smoke skipped (no timeout command)"
fi

echo "✓ flash complete"
