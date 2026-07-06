#!/usr/bin/env bash
# Leave USB download mode and run astrolabe175c after flash (ESP32-S3 USB-JTAG).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PORT="${1:-}"
if [[ -z "${PORT}" ]]; then
  PORT="$(ls /dev/cu.usbmodem* 2>/dev/null | head -1 || true)"
fi
if [[ -z "${PORT}" ]]; then
  echo "error: pass serial port (e.g. /dev/cu.usbmodem1301)" >&2
  exit 1
fi
IDF_PY="${HOME}/.espressif/python_env/idf5.5_py3.13_env/bin/python"
if [[ ! -x "${IDF_PY}" ]]; then
  IDF_PY="$(command -v python3)"
fi
echo "astrolabe175c_run: starting app on ${PORT}" >&2
"${IDF_PY}" -m esptool --chip esp32s3 -p "${PORT}" --before usb_reset --after hard_reset run >/dev/null
echo "astrolabe175c_run: if the screen stays dark, tap RESET once (do not hold BOOT)." >&2
