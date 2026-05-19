#!/usr/bin/env bash
# Capture USB serial and print an agent-friendly triage report.
# Writes artifacts/monitor/latest.log and latest-review.json.
#
# Usage:
#   ./scripts/console_review.sh [seconds] [send_line]
#   ASTROLABE_MONITOR_NO_RESET=1 ./scripts/console_review.sh 20
#   ./scripts/console_review.sh 45 "face 3"
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DURATION="${1:-30}"
SEND_LINE="${2:-}"
VENV_PY="$ROOT/mcp/astrolabe-esp/.venv/bin/python"
if [ ! -x "$VENV_PY" ]; then
  "$ROOT/mcp/astrolabe-esp/setup.sh"
fi
PORT="${ASTROLABE_SERIAL_PORT:-}"
if [ -z "$PORT" ]; then
  PORT=$(pio device list 2>/dev/null | awk '/usbmodem/{print $1; exit}' || true)
fi
if [ -z "$PORT" ]; then
  echo "No usbmodem port." >&2
  exit 1
fi
RESET_BOOT="True"
if [[ "${ASTROLABE_MONITOR_NO_RESET:-}" == "1" || "${ASTROLABE_MONITOR_NO_RESET:-}" == "true" ]]; then
  RESET_BOOT="False"
fi
STOP_PATTERN="${ASTROLABE_CONSOLE_STOP_PATTERN:-Mynah Astrolabe ready}"

export ASTROLABE_ROOT="$ROOT"
exec "$VENV_PY" -u - "$PORT" "$DURATION" "$SEND_LINE" "$RESET_BOOT" "$STOP_PATTERN" <<PY
import asyncio
import os
import sys
from pathlib import Path

ROOT = Path(os.environ["ASTROLABE_ROOT"])
MCP = ROOT / "mcp" / "astrolabe-esp"
sys.path.insert(0, str(MCP))
from server import _capture_and_review

port, duration_s, send_line, reset_boot, stop_pattern = (
    sys.argv[1],
    int(sys.argv[2]),
    sys.argv[3],
    sys.argv[4].lower() in ("1", "true", "yes"),
    sys.argv[5],
)

async def main() -> None:
    out = await _capture_and_review(
        duration_sec=duration_s,
        port=port,
        baud=115200,
        reset_boot=reset_boot,
        send_line=send_line,
        log_file="",
        stop_pattern=stop_pattern,
        tail_lines=80,
    )
    print(out)

asyncio.run(main())
PY
