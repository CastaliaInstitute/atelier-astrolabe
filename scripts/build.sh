#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

if command -v pio >/dev/null 2>&1; then
  exec pio run -e waveshare_s3_175 "$@"
fi

exec python3 -m platformio run -e waveshare_s3_175 "$@"
