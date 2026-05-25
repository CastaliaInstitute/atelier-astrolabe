#!/usr/bin/env bash
# Print the PlatformIO upload port for an Espressif USB-JTAG/serial device (VID 303A, PID 1001).
# Set ASTROLABE_UPLOAD_PORT to skip detection.
set -euo pipefail

port_available() {
  [[ -n "${1:-}" && -e "$1" ]]
}

if port_available "${ASTROLABE_UPLOAD_PORT:-}"; then
  echo "$ASTROLABE_UPLOAD_PORT"
  exit 0
fi
if [[ -n "${ASTROLABE_UPLOAD_PORT:-}" ]]; then
  echo "→ ASTROLABE_UPLOAD_PORT=${ASTROLABE_UPLOAD_PORT} missing; auto-detecting" >&2
fi

if port_available "${UPLOAD_PORT:-}"; then
  echo "$UPLOAD_PORT"
  exit 0
fi

esptool_python() {
  if [[ -x "$HOME/.platformio/penv/bin/python" ]]; then
    echo "$HOME/.platformio/penv/bin/python"
    return 0
  fi
  if command -v python3 >/dev/null 2>&1; then
    command -v python3
    return 0
  fi
  return 1
}

is_esp32_s3_port() {
  local port="$1"
  local py
  py="$(esptool_python)" || return 1
  "$py" -m esptool --port "$port" chip_id 2>/dev/null | grep -q "Chip is ESP32-S3"
}

if [[ "$(uname -s)" == "Darwin" ]]; then
  shopt -s nullglob
  candidates=(/dev/cu.usbmodem*)
  shopt -u nullglob
  if [[ ${#candidates[@]} -gt 1 ]]; then
    s3_candidates=()
    for dev in "${candidates[@]}"; do
      if is_esp32_s3_port "$dev"; then
        s3_candidates+=("$dev")
      fi
    done
    if [[ ${#s3_candidates[@]} -eq 1 ]]; then
      echo "${s3_candidates[0]}"
      exit 0
    fi
    if [[ ${#s3_candidates[@]} -gt 1 ]]; then
      echo "error: multiple ESP32-S3 usbmodem devices: ${s3_candidates[*]}; set ASTROLABE_UPLOAD_PORT" >&2
      exit 1
    fi
  fi
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
