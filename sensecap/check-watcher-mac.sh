#!/usr/bin/env bash
set -euo pipefail

PORT="${1:-/dev/cu.wchusbserial56D50202623}"
EXPECTED_MAC="${ASTROLABE_WATCHER_MAC:-d8:3b:da:75:c6:e4}"
PYTHON="${IDF_PYTHON_ENV_PATH:-$HOME/.espressif/python_env/idf5.2_py3.9_env}/bin/python"

if [[ ! -x "$PYTHON" ]]; then
  PYTHON="python3"
fi

if [[ ! -e "$PORT" ]]; then
  echo "Watcher port not found: $PORT" >&2
  exit 2
fi

output="$("$PYTHON" -m esptool --chip esp32s3 --port "$PORT" --baud 115200 \
  --before default_reset --after hard_reset chip_id 2>&1)"
printf '%s\n' "$output"

actual="$(printf '%s\n' "$output" | awk '/^MAC:/ {print $2; found=1} END {if (!found) exit 1}' | tail -n 1)"
if [[ "$actual" != "$EXPECTED_MAC" ]]; then
  echo "Unexpected Watcher MAC: got $actual, expected $EXPECTED_MAC" >&2
  exit 1
fi

echo "Watcher MAC verified: $actual"
