#!/usr/bin/env bash
# Print the upload port for an Espressif USB-JTAG/serial device (VID 303A, PID 1001).
# Set ASTROLABE_DEVICE_MAC to resolve a stable device identity. Set
# ASTROLABE_UPLOAD_PORT only when intentionally bypassing MAC resolution.
set -euo pipefail

port_available() {
  [[ -n "${1:-}" && -e "$1" ]]
}

if [[ -n "${ASTROLABE_DEVICE_MAC:-}" ]]; then
  if port_available "${ASTROLABE_UPLOAD_PORT:-}"; then
    resolved="$(./scripts/resolve_esp_port_by_mac.sh "$ASTROLABE_DEVICE_MAC" "$ASTROLABE_UPLOAD_PORT")"
  else
    resolved="$(./scripts/resolve_esp_port_by_mac.sh "$ASTROLABE_DEVICE_MAC")"
  fi
  echo "$resolved"
  exit 0
fi

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

if command -v pio >/dev/null 2>&1; then
  current_port=""
  wch_single_port=""
  while IFS= read -r line; do
    if [[ "$line" =~ ^(/dev/[^[:space:]]+|COM[0-9]+) ]]; then
      current_port="${BASH_REMATCH[1]}"
      continue
    fi
    if [[ "$line" == *"303A:1001"* && -n "$current_port" ]]; then
      echo "$current_port"
      exit 0
    fi
    # Waveshare ESP32-S3-Touch-AMOLED 1.85-style boards may enumerate through
    # WCH USB serial instead of Espressif native USB-JTAG. Prefer the single
    # serial bridge when the 1.75 native USB device is absent.
    if [[ "$line" == *"1A86:55D3"* && -n "$current_port" && "$current_port" == *usbmodem* ]]; then
      wch_single_port="$current_port"
    fi
  done < <(pio device list 2>/dev/null || true)
  if [[ -n "$wch_single_port" ]]; then
    echo "$wch_single_port"
    exit 0
  fi
fi

if [[ "$(uname -s)" == "Darwin" ]]; then
  shopt -s nullglob
  candidates=(/dev/cu.usbmodem*)
  native=()
  wch_single=()
  candidate_count=0
  native_count=0
  wch_single_count=0
  for dev in "${candidates[@]:-}"; do
    candidate_count=$((candidate_count + 1))
    case "$dev" in
      *5A360268091) wch_single+=("$dev"); wch_single_count=$((wch_single_count + 1)) ;;
      *56D5020262*) ;;
      *) native+=("$dev"); native_count=$((native_count + 1)) ;;
    esac
  done
  shopt -u nullglob
  if [[ $native_count -eq 1 ]]; then
    echo "${native[0]}"
    exit 0
  fi
  if [[ $native_count -gt 1 ]]; then
    echo "error: multiple native usbmodem devices: ${native[*]}; set ASTROLABE_UPLOAD_PORT" >&2
    exit 1
  fi
  if [[ $wch_single_count -eq 1 ]]; then
    echo "${wch_single[0]}"
    exit 0
  fi
  if [[ $candidate_count -gt 1 ]]; then
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
