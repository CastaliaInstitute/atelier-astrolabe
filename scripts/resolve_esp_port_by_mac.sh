#!/usr/bin/env bash
# Resolve the current serial port for an attached ESP device by MAC address.
#
#   ./scripts/resolve_esp_port_by_mac.sh a4:cb:8f:d6:42:60
#
# Port names can change whenever USB devices re-enumerate. Treat MAC as the
# stable identity and verify it immediately before upload/monitor operations.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

usage() {
  cat <<EOF
Usage: $(basename "$0") <mac> [port ...]

Prints the matching serial port. Fails unless exactly one attached device
reports the requested MAC.
EOF
}

normalize_mac() {
  printf "%s" "$1" | tr '[:upper:]' '[:lower:]' | tr -d ':-'
}

wanted="${1:-${ASTROLABE_DEVICE_MAC:-}}"
if [[ -z "$wanted" || "$wanted" == "-h" || "$wanted" == "--help" ]]; then
  usage
  [[ -z "$wanted" ]] && exit 1 || exit 0
fi
shift || true

wanted_norm="$(normalize_mac "$wanted")"
if [[ ${#wanted_norm} -ne 12 ]]; then
  echo "error: invalid MAC '${wanted}'" >&2
  exit 1
fi

matches=()
while IFS=$'\t' read -r port chip mac usb_serial; do
  [[ "$port" == "port" ]] && continue
  [[ -n "$port" && "$port" != "unknown" ]] || continue
  mac_norm="$(normalize_mac "${mac:-}")"
  usb_norm="$(normalize_mac "${usb_serial:-}")"
  if [[ "$mac_norm" == "$wanted_norm" || "$usb_norm" == "$wanted_norm" ]]; then
    matches+=("$port")
  fi
done < <("$ROOT/scripts/enumerate_esp_devices.sh" "$@")

if [[ ${#matches[@]} -eq 1 ]]; then
  echo "${matches[0]}"
  exit 0
fi

if [[ ${#matches[@]} -eq 0 ]]; then
  echo "error: no attached ESP device matched MAC ${wanted}" >&2
  "$ROOT/scripts/enumerate_esp_devices.sh" "$@" >&2 || true
  exit 1
fi

echo "error: multiple devices matched MAC ${wanted}: ${matches[*]}" >&2
exit 1
