#!/usr/bin/env bash
# Enumerate attached Espressif serial devices and capture chip/MAC mappings.
set -euo pipefail

if ! command -v pio >/dev/null 2>&1; then
  echo "error: PlatformIO 'pio' is required for the bundled esptool" >&2
  exit 1
fi

if [[ $# -gt 0 ]]; then
  ports=("$@")
else
  shopt -s nullglob
  if [[ "$(uname -s)" == "Darwin" ]]; then
    ports=(/dev/cu.usbmodem*)
  else
    ports=(/dev/ttyACM* /dev/ttyUSB*)
  fi
  shopt -u nullglob
fi

if [[ ${#ports[@]} -eq 0 ]]; then
  echo "error: no USB serial ports found" >&2
  exit 1
fi

printf "port\tchip\tmac\tusb_serial\n"
for port in "${ports[@]}"; do
  [[ -e "$port" ]] || continue
  out="$(pio pkg exec --package tool-esptoolpy -- esptool.py \
    --port "$port" --baud 115200 --connect-attempts 3 --no-stub read-mac 2>&1 || true)"
  chip="$(printf "%s\n" "$out" | sed -n 's/^Chip type:[[:space:]]*//p' | head -n 1)"
  mac="$(printf "%s\n" "$out" | sed -n 's/^MAC:[[:space:]]*//p' | tail -n 1)"
  usb_serial="$(
    pio device list 2>/dev/null |
      awk -v target="$port" '
        $0 == target { in_target=1; next }
        in_target && /^Hardware ID:/ {
          if (match($0, /SER=[^ ]+/)) {
            print substr($0, RSTART + 4, RLENGTH - 4)
          }
          exit
        }
        in_target && /^$/ { exit }
      ' || true
  )"
  printf "%s\t%s\t%s\t%s\n" "$port" "${chip:-unknown}" "${mac:-unknown}" "${usb_serial:-unknown}"
done
