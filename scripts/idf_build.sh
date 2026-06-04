#!/usr/bin/env bash
# ESP-IDF build helper for the opt-in idf/ pocket-watch project (Arduino hybrid).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IDF_PROJECT="${ROOT}/idf"
LIBDEPS="${ROOT}/.pio/libdeps/waveshare_s3_175"

astrolabe_idf_python() {
  if [[ -n "${ASTROLABE_IDF_PYTHON:-}" && -x "${ASTROLABE_IDF_PYTHON}" ]]; then
    echo "${ASTROLABE_IDF_PYTHON}"
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

if [[ -z "${IDF_PATH:-}" || ! -f "${IDF_PATH}/export.sh" ]]; then
  for export_sh in \
    "${IDF_PATH:-}/export.sh" \
    "${HOME}/esp/esp-idf/export.sh" \
    "${HOME}/.espressif/esp-idf/export.sh"; do
    if [[ -n "${export_sh}" && -f "${export_sh}" ]]; then
      # shellcheck disable=SC1090
      source "${export_sh}" >/dev/null
      break
    fi
  done
fi

if [[ -z "${IDF_PATH:-}" || ! -f "${IDF_PATH}/export.sh" ]]; then
  echo "idf_build: source ESP-IDF export.sh first (IDF 5.3–5.5)." >&2
  exit 127
fi
# shellcheck disable=SC1090
source "${IDF_PATH}/export.sh"

IDF_PY=(idf.py)
if resolved_py="$(astrolabe_idf_python)"; then
  export IDF_PYTHON_ENV_PATH="$(dirname "$(dirname "${resolved_py}")")"
  IDF_PY=("${resolved_py}" "${IDF_PATH}/tools/idf.py")
  echo "idf_build: using ${resolved_py}" >&2
fi

if [[ ! -d "${LIBDEPS}" ]]; then
  if ! command -v pio >/dev/null 2>&1; then
    echo "idf_build: missing ${LIBDEPS}; run: pio pkg install -e waveshare_s3_175" >&2
    exit 1
  fi
  echo "idf_build: installing PlatformIO libdeps (waveshare_s3_175)…" >&2
  (cd "${ROOT}" && pio pkg install -e waveshare_s3_175)
fi

cd "${IDF_PROJECT}"

# Patches for managed_components (arduino-esp32 vs IDF API gaps).
"${ROOT}/scripts/idf_apply_patches.sh"

# Regenerate sdkconfig when defaults add required Kconfig (e.g. BT for presence).
sdkconfig="${IDF_PROJECT}/sdkconfig"
defaults="${IDF_PROJECT}/sdkconfig.defaults"
if [[ -f "${sdkconfig}" && -f "${defaults}" ]]; then
  need_reconfig=false
  while IFS= read -r line; do
    [[ "${line}" =~ ^CONFIG_[A-Za-z0-9_]+=y$ ]] || continue
    key="${line%%=*}"
    if ! grep -q "^${key}=y" "${sdkconfig}" 2>/dev/null; then
      need_reconfig=true
      break
    fi
  done < "${defaults}"
  if [[ "${need_reconfig}" == true ]]; then
    echo "idf_build: sdkconfig missing keys from sdkconfig.defaults — re-running set-target" >&2
    rm -f "${sdkconfig}" "${IDF_PROJECT}/sdkconfig.old"
    "${IDF_PY[@]}" set-target esp32s3
  fi
elif [[ ! -f "${sdkconfig}" ]]; then
  "${IDF_PY[@]}" set-target esp32s3
fi

exec "${IDF_PY[@]}" "$@"
