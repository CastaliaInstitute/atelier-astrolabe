#!/usr/bin/env bash
# Factory flash for integration: erase + build + upload (16 MB default partitions).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
ENV="${PIO_ENV:-waveshare_s3_175}"
export PLATFORMIO_BUILD_DIR="${PLATFORMIO_BUILD_DIR:-$HOME/astrolabe-pio-build-integration}"
PORT="$(./scripts/detect_upload_port.sh)"
PY="${PLATFORMIO_PYTHON:-/opt/homebrew/opt/python@3.11/bin/python3.11}"
[[ -x "$PY" ]] || PY="$(command -v python3)"

echo "→ port: ${PORT}"
echo "→ erase_flash (required after prior 32MB OTA partition table)"
$PY -m esptool --chip esp32s3 --port "$PORT" erase_flash

echo "→ build ${ENV}"
./scripts/build.sh

echo "→ upload"
pio run -e "$ENV" -t upload --upload-port "$PORT"

if [[ -x "${ROOT}/scripts/monitor_capture.sh" ]]; then
  LOG="${ROOT}/artifacts/monitor/boot-integration-$(date +%s).log"
  mkdir -p "${ROOT}/artifacts/monitor"
  ASTROLABE_MONITOR_LOG="$LOG" ASTROLABE_SERIAL_PORT="$PORT" \
    "${ROOT}/scripts/monitor_capture.sh" 20 || true
  if grep -qE 'MVP ready|Astrolabe ready' "$LOG" 2>/dev/null; then
    echo "✓ boot OK"
  elif grep -q 'ESP-ROM' "$LOG" && ! grep -qE 'MVP ready|ready' "$LOG" 2>/dev/null; then
    echo "✗ ROM boot loop — see ${LOG}" >&2
    exit 1
  fi
fi

echo "✓ factory flash complete"
