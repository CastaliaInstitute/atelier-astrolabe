#!/usr/bin/env bash
# Print the PlatformIO upload port for an Espressif USB-JTAG/serial device (VID 303A, PID 1001).
# Set ASTROLABE_UPLOAD_PORT to skip detection.
set -euo pipefail

if [[ -n "${ASTROLABE_UPLOAD_PORT:-}" ]]; then
  echo "$ASTROLABE_UPLOAD_PORT"
  exit 0
fi

if [[ -n "${UPLOAD_PORT:-}" ]]; then
  echo "$UPLOAD_PORT"
  exit 0
fi

if command -v pio >/dev/null 2>&1; then
  while IFS= read -r line; do
    if [[ "$line" == *"303A:1001"* ]]; then
      want_port=1
      continue
    fi
    if [[ "${want_port:-0}" == "1" ]] && [[ "$line" =~ ^(/dev/|COM[0-9]) ]]; then
      echo "${line%% *}"
      exit 0
    fi
    want_port=0
  done < <(pio device list 2>/dev/null || true)
fi

if [[ "$(uname -s)" == "Darwin" ]]; then
  shopt -s nullglob
  candidates=(/dev/cu.usbmodem*)
  shopt -u nullglob
  if [[ ${#candidates[@]} -eq 1 ]]; then
    echo "${candidates[0]}"
    exit 0
  fi
  if [[ ${#candidates[@]} -gt 1 ]]; then
    echo "error: multiple usbmodem devices: ${candidates[*]}; set ASTROLABE_UPLOAD_PORT" >&2
    exit 1
  fi
fi

shopt -s nullglob
for dev in /dev/ttyACM* /dev/ttyUSB*; do
  echo "$dev"
  exit 0
done
shopt -u nullglob

echo "error: no Espressif upload port found (303A:1001); plug in watch and set ASTROLABE_UPLOAD_PORT" >&2
exit 1
