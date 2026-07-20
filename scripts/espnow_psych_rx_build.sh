#!/usr/bin/env bash
# Build/flash the ESP-NOW psychometer receiver under espnow_psych_rx/.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
if [[ -z "${IDF_PATH:-}" || ! -f "${IDF_PATH}/export.sh" ]]; then
  echo "error: source ESP-IDF first (export.sh)" >&2
  exit 1
fi
# shellcheck disable=SC1090
source "${IDF_PATH}/export.sh"

cd "${ROOT}/espnow_psych_rx"
if [[ ! -f sdkconfig ]]; then
  idf.py -D SDKCONFIG_DEFAULTS=sdkconfig.defaults set-target esp32s3
fi
exec idf.py "$@"
