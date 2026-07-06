#!/usr/bin/env bash
# Identify a USB-connected Waveshare AMOLED module before flashing astrolabe175c vs faculty18.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PORT="${1:-}"

if [[ -z "${PORT}" ]]; then
  PORT="$(ls /dev/cu.usbmodem* 2>/dev/null | head -1 || true)"
fi
if [[ -z "${PORT}" ]]; then
  echo "error: pass serial port (e.g. /dev/cu.usbmodem1101)" >&2
  exit 1
fi

IDF_PY="${HOME}/.espressif/python_env/idf5.5_py3.13_env/bin/python"
if [[ ! -x "${IDF_PY}" ]]; then
  IDF_PY="$(command -v python3)"
fi

echo "=== Astrolabe Waveshare identify ==="
echo "port: ${PORT}"
echo

FLASH_OUT="$("${IDF_PY}" -m esptool --chip esp32s3 -p "${PORT}" flash_id 2>&1)" || {
  echo "${FLASH_OUT}"
  echo "error: esptool flash_id failed (close serial monitor first)" >&2
  exit 1
}
echo "${FLASH_OUT}"
echo

FLASH_MB="$(echo "${FLASH_OUT}" | sed -n 's/.*Detected flash size: \([0-9]*\)MB.*/\1/p')"
MAC="$(echo "${FLASH_OUT}" | sed -n 's/.*MAC: \([0-9a-f:]*\).*/\1/p')"
PSRAM="$(echo "${FLASH_OUT}" | sed -n 's/.*Embedded PSRAM \([0-9]*\)MB.*/\1/p')"

echo "Summary:"
echo "  MAC:        ${MAC:-?}"
echo "  Flash:      ${FLASH_MB:-?} MB"
echo "  PSRAM:      ${PSRAM:-?} MB"
echo

if [[ "${FLASH_MB}" == "16" ]]; then
  echo "Likely module: ESP32-S3-Touch-AMOLED-1.75 (466×466, 16 MB flash)"
  echo "Firmware:      astrolabe175c (set CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y in astrolabe175c/sdkconfig)"
  echo "LCD reset:     GPIO 2 (Arduino) + GPIO 1 (1.75C BSP) — both pulsed in firmware"
elif [[ "${FLASH_MB}" == "32" ]]; then
  echo "Likely module: ESP32-S3-Touch-AMOLED-1.75C (466×466, 32 MB flash)"
  echo "Firmware:      astrolabe175c (CONFIG_ESPTOOLPY_FLASHSIZE_32MB=y)"
else
  echo "Flash size unclear — check Waveshare silkscreen / product page"
fi

echo
echo "After flashing astrolabe175c, confirm on-device:"
echo "  ./scripts/astrolabe175c_build.sh -p ${PORT} monitor"
echo "  (type: qa board)"
echo
echo "If qa board reports TCA9554=1 → this is the 1.8″ board:"
echo "  use ./scripts/faculty18_build.sh instead (368×448 SH8601)"
echo
echo "Board matrix:"
echo "  | Module | Flash | Display      | Firmware   |"
echo "  | 1.75C  | 32 MB | 466 CO5300   | astrolabe175c |"
echo "  | 1.75   | 16 MB | 466 CO5300   | astrolabe175c |"
echo "  | 1.8    | 16 MB | 368 SH8601   | faculty18  |"
