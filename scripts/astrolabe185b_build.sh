#!/usr/bin/env bash
# Build/flash the Waveshare 1.85B Astrolabe ESP-IDF project under astrolabe185b/.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
if [[ -z "${IDF_PATH:-}" || ! -f "${IDF_PATH}/export.sh" ]]; then
  echo "error: source ESP-IDF first (export.sh)" >&2
  exit 1
fi
# shellcheck disable=SC1090
source "${IDF_PATH}/export.sh"

ASTROLABE185B_USB_MODE="${ASTROLABE185B_USB_MODE:-dev}"
ASTROLABE185B_VARIANT="${ASTROLABE185B_VARIANT:-faculty}"
case "${ASTROLABE185B_VARIANT}" in
  faculty|cyber|claw|recovery|xdj) ;;
  *) echo "error: ASTROLABE185B_VARIANT must be faculty, cyber, claw, recovery, or xdj" >&2; exit 2 ;;
esac
ASTROLABE185B_FORCE_RECONFIGURE=0
ASTROLABE185B_CMAKE_ARGS=(-D "ASTROLABE185B_BUILD_VARIANT=${ASTROLABE185B_VARIANT}")
if [[ "${1:-}" == "usb-demo" ]]; then
  ASTROLABE185B_USB_MODE="msc"
  ASTROLABE185B_FORCE_RECONFIGURE=1
  ASTROLABE185B_CMAKE_ARGS=(-D ASTROLABE_USB_OTA_DEMO_BOOT=1)
  shift
fi

astrolabe185b_idf_python() {
  if [[ -n "${ASTROLABE185B_IDF_PYTHON:-}" && -x "${ASTROLABE185B_IDF_PYTHON}" ]]; then
    echo "${ASTROLABE185B_IDF_PYTHON}"
    return 0
  fi
  local candidate
  for candidate in \
    "${HOME}/.espressif/python_env/idf5.5_py3.14_env/bin/python" \
    "${HOME}/.espressif/python_env/idf5.5_py3.13_env/bin/python" \
    "${HOME}/.espressif/python_env/idf5.4_py3.13_env/bin/python" \
    "${HOME}/.espressif/python_env/idf5.3.3_py3.11_env/bin/python"; do
    if [[ -x "${candidate}" ]]; then
      echo "${candidate}"
      return 0
    fi
  done
  return 1
}

astrolabe185b_arg_port() {
  local prev=""
  local arg
  for arg in "$@"; do
    if [[ "${prev}" == "-p" || "${prev}" == "--port" ]]; then
      echo "${arg}"
      return 0
    fi
    case "${arg}" in
      -p?*) echo "${arg#-p}"; return 0 ;;
      --port=*) echo "${arg#--port=}"; return 0 ;;
    esac
    prev="${arg}"
  done
  return 1
}

astrolabe185b_reset_stale_cmake_cache() {
  local cache_file="$1"
  local expected_source="$2"
  local reset_path="$3"
  if [[ ! -f "${cache_file}" ]]; then
    return 0
  fi

  local cached_source=""
  cached_source="$("${PYTHON:-python3}" - "${cache_file}" <<'PY'
from pathlib import Path
import sys

cache = Path(sys.argv[1]).read_text(errors="ignore").splitlines()
for line in cache:
    if line.startswith("CMAKE_HOME_DIRECTORY:INTERNAL="):
        print(line.split("=", 1)[1])
        break
PY
)"

  if [[ -n "${cached_source}" && "${cached_source}" != "${expected_source}" ]]; then
    echo "astrolabe185b_build: clearing stale CMake cache under ${reset_path}" >&2
    rm -rf "${reset_path}"
  fi
}

astrolabe185b_flash_core() {
  local port="${1:-}"
  "${IDF_PY[@]}" "${ASTROLABE185B_CMAKE_ARGS[@]}" build
  if [[ -z "${port}" ]]; then
    echo "error: flash-core requires -p/--port" >&2
    return 1
  fi

  "${IDF_PY[0]}" "${IDF_PATH}/components/esptool_py/esptool/esptool.py" \
    --chip esp32s3 \
    --port "${port}" \
    --baud 460800 \
    --before default_reset \
    --after hard_reset \
    write_flash --flash_mode dio --flash_size 16MB --flash_freq 80m \
    0x0 build/bootloader/bootloader.bin \
    0x8000 build/partition_table/partition-table.bin \
    0x1a000 build/ota_data_initial.bin \
    0x20000 build/astrolabe185b.bin
}

cd "${ROOT}/astrolabe185b"

astrolabe185b_reset_stale_cmake_cache \
  "${ROOT}/astrolabe185b/build/CMakeCache.txt" \
  "${ROOT}/astrolabe185b" \
  "${ROOT}/astrolabe185b/build"
astrolabe185b_reset_stale_cmake_cache \
  "${ROOT}/astrolabe185b/build/bootloader/CMakeCache.txt" \
  "${IDF_PATH}/components/bootloader/subproject" \
  "${ROOT}/astrolabe185b/build/bootloader"
rm -rf "${ROOT}/astrolabe185b/build/bootloader-prefix" "${ROOT}/astrolabe185b/build/bootloader-prefix_tmp"

IDF_PY=(idf.py)
if resolved_py="$(astrolabe185b_idf_python)"; then
  IDF_PY=("${resolved_py}" "${IDF_PATH}/tools/idf.py")
  echo "astrolabe185b_build: using ${resolved_py}" >&2
fi

sdkconfig_defaults="sdkconfig.defaults"
if [[ "${ASTROLABE185B_VARIANT}" == "xdj" ]]; then
  sdkconfig_defaults="${sdkconfig_defaults};sdkconfig.xdj.defaults"
fi
if [[ "${ASTROLABE185B_USB_MODE}" == "msc" ]]; then
  sdkconfig_defaults="${sdkconfig_defaults};sdkconfig.usb_msc.defaults"
fi

if [[ ! -f sdkconfig ]]; then
  "${IDF_PY[@]}" "${ASTROLABE185B_CMAKE_ARGS[@]}" set-target esp32s3
else
  if [[ "${ASTROLABE185B_FORCE_RECONFIGURE}" -eq 1 ]]; then
    "${IDF_PY[@]}" "${ASTROLABE185B_CMAKE_ARGS[@]}" -D SDKCONFIG_DEFAULTS="${sdkconfig_defaults}" reconfigure
  else
    "${IDF_PY[@]}" "${ASTROLABE185B_CMAKE_ARGS[@]}" -D SDKCONFIG_DEFAULTS="${sdkconfig_defaults}" reconfigure >/dev/null 2>&1 || true
  fi
fi

if [[ "${1:-}" == "flash-core" ]]; then
  shift
  port=""
  if [[ $# -gt 0 ]] && port="$(astrolabe185b_arg_port "$@")"; then
    :
  fi
  astrolabe185b_flash_core "${port}"
  exit 0
fi

"${IDF_PY[@]}" "${ASTROLABE185B_CMAKE_ARGS[@]}" "$@"
