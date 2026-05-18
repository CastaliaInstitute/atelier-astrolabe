#!/usr/bin/env bash
# Install PlatformIO + MCP venv deps on self-hosted macOS (no `pip` on PATH) and Linux.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

if command -v pip >/dev/null 2>&1; then
  pip install platformio
elif command -v pip3 >/dev/null 2>&1; then
  pip3 install platformio
else
  python3 -m pip install platformio
fi

./mcp/astrolabe-esp/setup.sh
