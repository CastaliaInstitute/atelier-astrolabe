#!/usr/bin/env bash
# Build/flash the Waveshare 1.8″ Faculty ESP-IDF project under faculty18/.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
if [[ -z "${IDF_PATH:-}" || ! -f "${IDF_PATH}/export.sh" ]]; then
  echo "error: source ESP-IDF first (export.sh)" >&2
  exit 1
fi
# shellcheck disable=SC1090
source "${IDF_PATH}/export.sh"

faculty18_idf_python() {
  if [[ -n "${FACULTY18_IDF_PYTHON:-}" && -x "${FACULTY18_IDF_PYTHON}" ]]; then
    echo "${FACULTY18_IDF_PYTHON}"
    return 0
  fi
  local candidate
  for candidate in \
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

cd "${ROOT}/faculty18"
IDF_PY=(idf.py)
if resolved_py="$(faculty18_idf_python)"; then
  IDF_PY=("${resolved_py}" "${IDF_PATH}/tools/idf.py")
  echo "faculty18_build: using ${resolved_py}" >&2
fi

if [[ ! -f sdkconfig ]]; then
  "${IDF_PY[@]}" set-target esp32s3
else
  # Apply new sdkconfig.defaults keys (e.g. SPIRAM) without wiping user overrides.
  "${IDF_PY[@]}" -D SDKCONFIG_DEFAULTS=sdkconfig.defaults reconfigure >/dev/null 2>&1 || true
fi
exec "${IDF_PY[@]}" "$@"
