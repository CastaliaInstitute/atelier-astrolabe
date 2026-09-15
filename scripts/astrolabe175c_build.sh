#!/usr/bin/env bash
# Build/flash the Waveshare 1.75C Astrolabe ESP-IDF project under astrolabe175c/.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
if [[ -z "${IDF_PATH:-}" || ! -f "${IDF_PATH}/export.sh" ]]; then
  echo "error: source ESP-IDF first (export.sh)" >&2
  exit 1
fi
# shellcheck disable=SC1090
source "${IDF_PATH}/export.sh"

ASTROLABE175C_FORCE_RECONFIGURE=0
ASTROLABE175C_VARIANT="${ASTROLABE175C_VARIANT:-faculty}"
case "${ASTROLABE175C_VARIANT}" in
  faculty|cyber|lunasay|claw) ;;
  *) echo "error: ASTROLABE175C_VARIANT must be faculty, cyber, lunasay, or claw" >&2; exit 2 ;;
esac
ASTROLABE175C_CMAKE_ARGS=(
  -D ASTROLABE_USB_OTA_DEMO_BOOT=0
  -D "ASTROLABE175C_BUILD_VARIANT=${ASTROLABE175C_VARIANT}"
  -D "ASTROLABE_OTA_AUTO_INTERVAL_S=${ASTROLABE175C_OTA_AUTO_INTERVAL_S:-60}"
  -D "ASTROLABE_VOICE_HTTP_URL=${ASTROLABE175C_VOICE_HTTP_URL:-}"
  -D "ASTROLABE_VOICE_STREAM_URL=${ASTROLABE175C_VOICE_STREAM_URL:-}"
)
ASTROLABE175C_FORCE_RECONFIGURE=1
if [[ -n "${ASTROLABE175C_OTA_AUTO_INTERVAL_S:-}" ]]; then
  ASTROLABE175C_FORCE_RECONFIGURE=1
fi
if [[ -n "${ASTROLABE175C_VOICE_HTTP_URL:-}" ]]; then
  ASTROLABE175C_FORCE_RECONFIGURE=1
fi
if [[ -n "${ASTROLABE175C_VOICE_STREAM_URL:-}" ]]; then
  ASTROLABE175C_FORCE_RECONFIGURE=1
fi
if [[ "${1:-}" == "usb-demo" ]]; then
  if [[ "${ASTROLABE175C_VARIANT}" != "cyber" ]]; then
    echo "error: usb-demo is a Cyber-only build; set ASTROLABE175C_VARIANT=cyber" >&2
    exit 2
  fi
  ASTROLABE175C_FORCE_RECONFIGURE=1
  ASTROLABE175C_CMAKE_ARGS+=(-D ASTROLABE_USB_OTA_DEMO_BOOT=1)
  shift
fi

astrolabe175c_should_audit_lvgl() {
  local arg
  for arg in "$@"; do
    case "${arg}" in
      build|all|flash|app-flash|encrypted-flash|encrypted-app-flash)
        return 0
        ;;
    esac
  done
  if [[ $# -eq 0 ]]; then
    return 0
  fi
  return 1
}

astrolabe175c_idf_python() {
  if [[ -n "${ASTROLABE175C_IDF_PYTHON:-}" && -x "${ASTROLABE175C_IDF_PYTHON}" ]]; then
    echo "${ASTROLABE175C_IDF_PYTHON}"
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

astrolabe175c_should_sync_time() {
  local arg
  for arg in "$@"; do
    case "${arg}" in
      flash|app-flash|encrypted-flash|encrypted-app-flash)
        return 0
        ;;
    esac
  done
  return 1
}

astrolabe175c_arg_port() {
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

astrolabe175c_reset_stale_cmake_cache() {
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
    echo "astrolabe175c_build: clearing stale CMake cache under ${reset_path}" >&2
    rm -rf "${reset_path}"
  fi
}

astrolabe175c_reset_stale_sdkconfig() {
  local sdkconfig_file="$1"
  local project_dir="$2"
  local defaults_file="$3"
  if [[ ! -f "${sdkconfig_file}" ]]; then
    return 0
  fi

  local partition_file=""
  partition_file="$(sed -n 's/^CONFIG_PARTITION_TABLE_FILENAME="\(.*\)"$/\1/p' "${sdkconfig_file}" | tail -1)"
  if [[ -n "${partition_file}" && ! -f "${project_dir}/${partition_file}" ]]; then
    echo "astrolabe175c_build: removing stale sdkconfig with missing partition table ${partition_file}" >&2
    rm -f "${sdkconfig_file}"
    return 0
  fi

  # A generated sdkconfig retains a previous choice value even when a checked-in
  # default changes it. In particular, an old USB-NCM selection must not survive
  # after tethering is disabled for a release build.
  local configured_net_mode=""
  local default_net_mode=""
  configured_net_mode="$(sed -n 's/^CONFIG_TINYUSB_NET_MODE_\([A-Z_]*\)=y$/\1/p' "${sdkconfig_file}" | tail -1)"
  default_net_mode="$(sed -n 's/^CONFIG_TINYUSB_NET_MODE_\([A-Z_]*\)=y$/\1/p' "${defaults_file}" | tail -1)"
  if [[ -n "${configured_net_mode}" && -n "${default_net_mode}" &&
        "${configured_net_mode}" != "${default_net_mode}" ]]; then
    echo "astrolabe175c_build: removing stale sdkconfig USB network mode ${configured_net_mode} (default ${default_net_mode})" >&2
    rm -f "${sdkconfig_file}"
  fi
}

astrolabe175c_flash_core() {
  local port="${1:-}"
  "${IDF_PY[@]}" "${ASTROLABE175C_CMAKE_ARGS[@]+"${ASTROLABE175C_CMAKE_ARGS[@]}"}" build
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
    write_flash --flash_mode dio --flash_size 32MB --flash_freq 80m \
    0x0 build/bootloader/bootloader.bin \
    0x8000 build/partition_table/partition-table.bin \
    0x1a000 build/ota_data_initial.bin \
    0x20000 build/astrolabe175c.bin

  "${PYTHON:-python3}" "${ROOT}/scripts/astrolabe175c_set_time.py" --port "${port}" || true
}

cd "${ROOT}/astrolabe175c"
if [[ "${ASTROLABE175C_SKIP_LVGL_AUDIT:-0}" != "1" ]] && astrolabe175c_should_audit_lvgl "$@"; then
  "${PYTHON:-python3}" "${ROOT}/scripts/audit_lvgl_port.py"
fi

astrolabe175c_reset_stale_sdkconfig \
  "${ROOT}/astrolabe175c/sdkconfig" \
  "${ROOT}/astrolabe175c" \
  "${ROOT}/astrolabe175c/sdkconfig.defaults"
astrolabe175c_reset_stale_cmake_cache \
  "${ROOT}/astrolabe175c/build/CMakeCache.txt" \
  "${ROOT}/astrolabe175c" \
  "${ROOT}/astrolabe175c/build"
astrolabe175c_reset_stale_cmake_cache \
  "${ROOT}/astrolabe175c/build/bootloader/CMakeCache.txt" \
  "${IDF_PATH}/components/bootloader/subproject" \
  "${ROOT}/astrolabe175c/build/bootloader"
rm -rf "${ROOT}/astrolabe175c/build/bootloader-prefix" "${ROOT}/astrolabe175c/build/bootloader-prefix_tmp"

IDF_PY=(idf.py)
if resolved_py="$(astrolabe175c_idf_python)"; then
  IDF_PY=("${resolved_py}" "${IDF_PATH}/tools/idf.py")
  echo "astrolabe175c_build: using ${resolved_py}" >&2
fi

if [[ ! -f sdkconfig ]]; then
  "${IDF_PY[@]}" "${ASTROLABE175C_CMAKE_ARGS[@]+"${ASTROLABE175C_CMAKE_ARGS[@]}"}" -D SDKCONFIG_DEFAULTS=sdkconfig.defaults set-target esp32s3
else
  "${IDF_PY[@]}" "${ASTROLABE175C_CMAKE_ARGS[@]+"${ASTROLABE175C_CMAKE_ARGS[@]}"}" -D SDKCONFIG_DEFAULTS=sdkconfig.defaults reconfigure >/dev/null 2>&1 || true
  if [[ "${ASTROLABE175C_FORCE_RECONFIGURE}" == "1" ]]; then
    "${IDF_PY[@]}" "${ASTROLABE175C_CMAKE_ARGS[@]+"${ASTROLABE175C_CMAKE_ARGS[@]}"}" reconfigure
  fi
fi

if [[ "${1:-}" == "flash-core" ]]; then
  shift
  port=""
  if [[ $# -gt 0 ]] && port="$(astrolabe175c_arg_port "$@")"; then
    :
  fi
  astrolabe175c_flash_core "${port}"
  exit 0
fi

"${IDF_PY[@]}" "${ASTROLABE175C_CMAKE_ARGS[@]+"${ASTROLABE175C_CMAKE_ARGS[@]}"}" "$@"
status=$?
if [[ "${status}" -eq 0 && "${ASTROLABE175C_SKIP_TIME_SYNC:-0}" != "1" ]] && astrolabe175c_should_sync_time "$@"; then
  port_args=()
  if port="$(astrolabe175c_arg_port "$@")"; then
    port_args=(--port "${port}")
  fi
  "${PYTHON:-python3}" "${ROOT}/scripts/astrolabe175c_set_time.py" "${port_args[@]}"
fi
exit "${status}"
