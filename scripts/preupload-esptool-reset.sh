#!/usr/bin/env bash
# ESP32-S3 USB Serial/JTAG: ask ROM bootloader via esptool usb_reset (best-effort).
set -euo pipefail
PORT="${1:?port}"
PY="${ASTROLABE_CI_VENV:-${HOME}/.astrolabe-ci-venv}/bin/python}"
if ! command -v pio >/dev/null 2>&1; then
  exit 0
fi
pio pkg exec -p "tool-esptoolpy" -- esptool.py \
  --chip esp32s3 \
  --port "$PORT" \
  --before usb_reset \
  --after no_reset \
  chip_id >/dev/null 2>&1 && echo "→ esptool usb_reset: chip detected" || true
