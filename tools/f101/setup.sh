#!/usr/bin/env bash
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
if [[ ! -x "$HERE/.venv/bin/python" ]]; then
  python3 -m venv --without-pip "$HERE/.venv"
fi
pip --python "$HERE/.venv/bin/python" install -r "$HERE/requirements.txt"
"$HERE/.venv/bin/python" "$HERE/provision.py"
bash "$HERE/android/build.sh"
codex mcp add astrolabe-f101 -- "$HERE/.venv/bin/python" "$HERE/server.py"
codex mcp get astrolabe-f101
