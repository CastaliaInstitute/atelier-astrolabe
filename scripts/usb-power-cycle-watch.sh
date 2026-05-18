#!/usr/bin/env bash
# Cycle VBUS on a uhubctl-compatible hub port (watch on that port only).
#
# Setup (once, after hub arrives):
#   brew install uhubctl
#   uhubctl                    # note Location (e.g. 2-1) and port number for the watch
#   export ASTROLABE_UHUBCTL_LOCATION=2-1
#   export ASTROLABE_UHUBCTL_PORT=2
#   export ASTROLABE_USB_POWER_CYCLE=1
#
# Or search by device name (less reliable if multiple Espressif devices):
#   export ASTROLABE_UHUBCTL_SEARCH=Espressif
#
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

mode="${ASTROLABE_USB_POWER_CYCLE:-auto}"
if [[ "$mode" == "0" ]]; then
  exit 0
fi

if ! command -v uhubctl >/dev/null 2>&1; then
  if [[ "$mode" == "1" ]]; then
    echo "error: ASTROLABE_USB_POWER_CYCLE=1 but uhubctl not installed (brew install uhubctl)" >&2
    exit 1
  fi
  echo "→ uhubctl not installed; skip USB power cycle"
  exit 0
fi

LOC="${ASTROLABE_UHUBCTL_LOCATION:-}"
PORT="${ASTROLABE_UHUBCTL_PORT:-}"
SEARCH="${ASTROLABE_UHUBCTL_SEARCH:-Espressif}"
DELAY="${ASTROLABE_UHUBCTL_CYCLE_DELAY:-2}"

cycle_hub() {
  if [[ -n "$LOC" && -n "$PORT" ]]; then
    echo "→ USB power cycle hub ${LOC} port ${PORT}"
    uhubctl -l "$LOC" -p "$PORT" -a cycle -d "$DELAY"
    return 0
  fi
  if [[ -n "$SEARCH" ]]; then
    echo "→ USB power cycle (search: ${SEARCH})"
    uhubctl -s "$SEARCH" -a cycle -d "$DELAY"
    return 0
  fi
  return 1
}

if ! cycle_hub 2>/dev/null; then
  if [[ "$mode" == "1" ]]; then
    echo "error: USB power cycle failed — set ASTROLABE_UHUBCTL_LOCATION and ASTROLABE_UHUBCTL_PORT" >&2
    echo "Run: uhubctl" >&2
    uhubctl 2>&1 || true
    exit 1
  fi
  echo "→ USB power cycle skipped (set ASTROLABE_UHUBCTL_LOCATION + ASTROLABE_UHUBCTL_PORT from: uhubctl)"
  exit 0
fi

WAIT="${ASTROLABE_USB_POWER_WAIT_SEC:-30}"
echo "→ waiting for watch USB (up to ${WAIT}s)"
for ((i = 0; i < WAIT; i++)); do
  if ./scripts/detect_upload_port.sh >/dev/null 2>&1; then
    echo "→ watch USB up"
    exit 0
  fi
  sleep 1
done

if [[ "$mode" == "1" ]]; then
  echo "error: watch port missing after USB power cycle" >&2
  exit 1
fi
echo "warning: watch port not seen ${WAIT}s after power cycle" >&2
