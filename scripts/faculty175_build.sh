#!/usr/bin/env bash
# Build/flash the Waveshare 1.75″ Faculty ESP-IDF project under faculty175/.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
if [[ -z "${IDF_PATH:-}" || ! -f "${IDF_PATH}/export.sh" ]]; then
  echo "error: source ESP-IDF first (export.sh)" >&2
  exit 1
fi
# shellcheck disable=SC1090
source "${IDF_PATH}/export.sh"

faculty175_should_audit_lvgl() {
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

faculty175_idf_python() {
  if [[ -n "${FACULTY175_IDF_PYTHON:-}" && -x "${FACULTY175_IDF_PYTHON}" ]]; then
    echo "${FACULTY175_IDF_PYTHON}"
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

faculty175_should_sync_time() {
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

faculty175_arg_port() {
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

cd "${ROOT}/faculty175"
if [[ "${FACULTY175_SKIP_LVGL_AUDIT:-0}" != "1" ]] && faculty175_should_audit_lvgl "$@"; then
  "${PYTHON:-python3}" "${ROOT}/scripts/audit_lvgl_port.py"
fi

IDF_PY=(idf.py)
if resolved_py="$(faculty175_idf_python)"; then
  IDF_PY=("${resolved_py}" "${IDF_PATH}/tools/idf.py")
  echo "faculty175_build: using ${resolved_py}" >&2
fi

if [[ ! -f sdkconfig ]]; then
  "${IDF_PY[@]}" set-target esp32s3
else
  "${IDF_PY[@]}" -D SDKCONFIG_DEFAULTS=sdkconfig.defaults reconfigure >/dev/null 2>&1 || true
fi
"${IDF_PY[@]}" "$@"
status=$?
if [[ "${status}" -eq 0 && "${FACULTY175_SKIP_TIME_SYNC:-0}" != "1" ]] && faculty175_should_sync_time "$@"; then
  port_args=()
  if port="$(faculty175_arg_port "$@")"; then
    port_args=(--port "${port}")
  fi
  "${PYTHON:-python3}" "${ROOT}/scripts/faculty175_set_time.py" "${port_args[@]}"
fi
exit "${status}"
