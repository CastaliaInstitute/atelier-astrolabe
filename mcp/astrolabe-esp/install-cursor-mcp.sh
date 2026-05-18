#!/usr/bin/env bash
# Add astrolabe-esp to ~/.cursor/mcp.json (idempotent snippet for manual merge if needed).
set -euo pipefail
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../.." && pwd)"
"$DIR/setup.sh"
PY="$DIR/.venv/bin/python"
SERVER="$DIR/server.py"
GLOBAL="${HOME}/.cursor/mcp.json"
echo "Add this block to mcpServers in ${GLOBAL}:"
echo ""
cat <<EOF
    "astrolabe-esp": {
      "command": "${PY}",
      "args": ["${SERVER}"],
      "cwd": "${ROOT}",
      "env": {
        "ASTROLABE_ROOT": "${ROOT}"
      }
    }
EOF
echo ""
echo "Then reload MCP in Cursor."
