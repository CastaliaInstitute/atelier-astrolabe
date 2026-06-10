#!/usr/bin/env bash
# Build, flash, time-sync, and run the Astrolabe Faculty STT/TTS voice proof.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PORT="${FACULTY175_VOICE_PORT:-${ASTROLABE_UPLOAD_PORT:-${UPLOAD_PORT:-${ESPPORT:-}}}}"
MAC="${FACULTY175_DEVICE_MAC:-${ASTROLABE_DEVICE_MAC:-a0:f2:62:e3:06:44}}"
STT_MS="${FACULTY175_VOICE_STT_MS:-7000}"
WAIT_SECONDS="${FACULTY175_VOICE_WAIT_SECONDS:-0}"
ALLOW_USBMODEM1101="${FACULTY175_ALLOW_USBMODEM1101:-0}"

cd "${ROOT}"

resolve_port() {
  local resolved="${PORT}"
  if [[ -z "${resolved}" ]]; then
    if [[ -x "${ROOT}/scripts/resolve_esp_port_by_mac.sh" ]]; then
      resolved="$("${ROOT}/scripts/resolve_esp_port_by_mac.sh" "${MAC}" 2>/dev/null || true)"
    fi
  fi
  if [[ -z "${resolved}" ]]; then
    local ports
    ports="$(find /dev -maxdepth 1 -name 'cu.usbmodem*' -print 2>/dev/null | sort)"
    if [[ -n "${ports}" && "$(printf '%s\n' "${ports}" | wc -l | tr -d ' ')" == "1" ]]; then
      resolved="${ports}"
    fi
  fi
  printf '%s' "${resolved}"
}

if [[ -z "${PORT}" && "${WAIT_SECONDS}" != "0" ]]; then
  echo "faculty175_voice_e2e: waiting up to ${WAIT_SECONDS}s for Astrolabe USB serial..."
  deadline=$((SECONDS + WAIT_SECONDS))
  while [[ "${SECONDS}" -lt "${deadline}" ]]; do
    PORT="$(resolve_port)"
    if [[ -n "${PORT}" && -e "${PORT}" ]]; then
      break
    fi
    sleep 1
  done
fi

if [[ -z "${PORT}" ]]; then
  if [[ -x "${ROOT}/scripts/resolve_esp_port_by_mac.sh" ]]; then
    PORT="$("${ROOT}/scripts/resolve_esp_port_by_mac.sh" "${MAC}" 2>/dev/null || true)"
  fi
fi
PORT="$(resolve_port)"
if [[ -z "${PORT}" || ! -e "${PORT}" ]]; then
  echo "error: Astrolabe serial port not found; set ASTROLABE_DEVICE_MAC, FACULTY175_VOICE_PORT/ASTROLABE_UPLOAD_PORT, set FACULTY175_VOICE_WAIT_SECONDS, or reconnect the watch" >&2
  exit 2
fi
if [[ "${PORT}" == "/dev/cu.usbmodem1101" && "${ALLOW_USBMODEM1101}" != "1" ]]; then
  echo "error: refusing to flash ${PORT}" >&2
  echo "       set FACULTY175_ALLOW_USBMODEM1101=1 only after confirming this is the intended target" >&2
  exit 3
fi

if [[ -z "${IDF_PATH:-}" || ! -f "${IDF_PATH}/export.sh" ]]; then
  export IDF_PATH="${HOME}/esp/esp-idf"
fi
# shellcheck disable=SC1091
source "${IDF_PATH}/export.sh" >/dev/null

echo "faculty175_voice_e2e: port=${PORT} mac=${MAC}"
idf.py -C "${ROOT}/faculty175" build
python -m esptool --chip esp32s3 -p "${PORT}" -b 460800 \
  --before default_reset --after hard_reset write_flash \
  --flash_mode dio --flash_size 32MB --flash_freq 80m \
  0x20000 "${ROOT}/faculty175/build/astrolabe_faculty175.bin"

python3 "${ROOT}/scripts/faculty175_set_time.py" --port "${PORT}"
python3 "${ROOT}/scripts/faculty175_voice_validate.py" --port "${PORT}" --stt-ms "${STT_MS}" --host-audio-test
