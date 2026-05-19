#!/usr/bin/env bash
# ESP32-S3 USB Serial/JTAG: exit ROM bootloader without relying on RTS/EN.
set -euo pipefail
PORT="${1:?port}"

if ! command -v pio >/dev/null 2>&1; then
  exit 0
fi

pio pkg exec -p "tool-esptoolpy" -- esptool.py \
  --chip esp32s3 \
  --port "$PORT" \
  --after watchdog_reset \
  read_mac >/dev/null 2>&1 && echo "→ esptool watchdog_reset: app boot requested" || true
