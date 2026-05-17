#!/usr/bin/env bash
set -euo pipefail
DIR="$(cd "$(dirname "$0")" && pwd)"
python3 -m venv "$DIR/.venv"
"$DIR/.venv/bin/pip" install -q -r "$DIR/requirements.txt"
echo "OK: $DIR/.venv"
echo "Enable astrolabe-esp in .cursor/mcp.json and reload Cursor MCP."
