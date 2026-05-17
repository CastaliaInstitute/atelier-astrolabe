#!/usr/bin/env bash
# Live USB CDC console (115200) with PlatformIO exception decoder.
# Usage: ./scripts/monitor_serial.sh [port]
#   ASTROLABE_SERIAL_PORT=/dev/cu.usbmodem1101 ./scripts/monitor_serial.sh
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
PORT="${1:-${ASTROLABE_SERIAL_PORT:-${MYNAH_SERIAL_PORT:-}}}"
if [ -z "$PORT" ]; then
  PORT=$(pio device list 2>/dev/null | awk '/usbmodem/{print $1; exit}' || true)
fi
if [ -z "$PORT" ]; then
  echo "No usbmodem port — plug in the watch (303A:1001)." >&2
  exit 1
fi
echo "Serial monitor port=$PORT (Ctrl+C to stop)"
exec pio device monitor -e waveshare_s3_175 --port "$PORT" --baud 115200
