#!/usr/bin/env bash
# ESP-IDF build helper for the opt-in idf/ project.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IDF_PROJECT="${ROOT}/idf"

if ! command -v idf.py >/dev/null 2>&1; then
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

if ! command -v idf.py >/dev/null 2>&1; then
  echo "idf.py not found. Install ESP-IDF 5.3-5.5 and source export.sh first." >&2
  exit 127
fi

cd "${IDF_PROJECT}"
if [[ ! -f sdkconfig ]]; then
  export IDF_TARGET="${IDF_TARGET:-esp32s3}"
fi
idf.py "$@"
