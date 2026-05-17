#!/usr/bin/env bash
# Capture serial log to artifacts/monitor/ (for agents or CI).
# Usage: ./scripts/monitor_capture.sh [seconds] [trigger_line]
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DURATION="${1:-90}"
TRIGGER="${2:-}"
mkdir -p "$ROOT/artifacts/monitor"
export ASTROLABE_MONITOR_LOG="${ASTROLABE_MONITOR_LOG:-$ROOT/artifacts/monitor/capture-$(date +%s).log}"
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
NO_RESET="${ASTROLABE_MONITOR_NO_RESET:-}"
RESET_BOOT="True"
if [[ "$NO_RESET" == "1" || "$NO_RESET" == "true" ]]; then
  RESET_BOOT="False"
fi

"$VENV_PY" -u - "$PORT" "$DURATION" "$TRIGGER" "$ASTROLABE_MONITOR_LOG" "$RESET_BOOT" <<PY
import sys
from pathlib import Path
sys.path.insert(0, "${ROOT}/mcp/astrolabe-esp")
from server import _serial_capture

port, duration_s, trigger, logpath, reset_boot = (
    sys.argv[1],
    int(sys.argv[2]),
    sys.argv[3],
    Path(sys.argv[4]),
    sys.argv[5].lower() in ("1", "true", "yes"),
)
lines, log = _serial_capture(port, 115200, duration_s, trigger, reset_boot, logpath)
for ln in lines[-200:]:
    print(ln)
print(f"# log={log} lines={len(lines)}")
PY
