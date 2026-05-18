#!/usr/bin/env bash
# Fail fast if the Waveshare ESP32-S3 watch is not on USB (303A:1001 / usbmodem).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

if ! PORT="$(./scripts/detect_upload_port.sh 2>&1)"; then
  echo "error: no watch upload port — plug in ESP32-S3 (303A:1001) and retry" >&2
  exit 1
fi
if [[ ! -e "$PORT" ]]; then
  echo "error: port ${PORT} not present — check cable and ASTROLABE_UPLOAD_PORT" >&2
  exit 1
fi
echo "→ watch port ${PORT}"
