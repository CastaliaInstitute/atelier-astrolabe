#!/usr/bin/env bash
# Interactive JTAG debug via PlatformIO (OpenOCD + GDB). Serial console is still on USB CDC.
# Usage: ./scripts/monitor_jtag_gdb.sh
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
echo "Building debug ELF (waveshare_s3_175_debug)…"
pio run -e waveshare_s3_175_debug
echo "Starting pio debug (OpenOCD + GDB). In another terminal: ./scripts/monitor_serial.sh"
exec pio debug -e waveshare_s3_175_debug
