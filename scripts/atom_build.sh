#!/usr/bin/env bash
# Build/flash the AtomS3R Wand ESP-IDF project under atom/.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
if [[ -z "${IDF_PATH:-}" || ! -f "${IDF_PATH}/export.sh" ]]; then
  echo "error: source ESP-IDF first (export.sh)" >&2
  exit 1
fi
# shellcheck disable=SC1090
source "${IDF_PATH}/export.sh"
cd "${ROOT}/atom"
if [[ ! -f sdkconfig ]]; then
  idf.py set-target esp32s3
fi
exec idf.py "$@"
