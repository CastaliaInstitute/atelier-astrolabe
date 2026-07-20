#!/usr/bin/env bash
# Build/flash the Elecrow CrowPanel 1.28 rotary ESP32-S3 target under elecrow128/.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
if [[ -z "${IDF_PATH:-}" || ! -f "${IDF_PATH}/export.sh" ]]; then
  echo "error: source ESP-IDF first (export.sh)" >&2
  exit 1
fi
# shellcheck disable=SC1090
source "${IDF_PATH}/export.sh"

elecrow128_idf_python() {
  if [[ -n "${ELECROW128_IDF_PYTHON:-}" && -x "${ELECROW128_IDF_PYTHON}" ]]; then
    echo "${ELECROW128_IDF_PYTHON}"
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

cd "${ROOT}/elecrow128"
IDF_PY=(idf.py)
if resolved_py="$(elecrow128_idf_python)"; then
  IDF_PY=("${resolved_py}" "${IDF_PATH}/tools/idf.py")
  echo "elecrow128_build: using ${resolved_py}" >&2
fi

if [[ ! -f sdkconfig ]]; then
  "${IDF_PY[@]}" -D SDKCONFIG_DEFAULTS=sdkconfig.defaults set-target esp32s3
fi
exec "${IDF_PY[@]}" "$@"
