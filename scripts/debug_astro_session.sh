#!/usr/bin/env bash
# Debugger workflow: flash debug ELF → JTAG set Astrology face → serial `astro` → monitor.
# Usage: ./scripts/debug_astro_session.sh [monitor_seconds]
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
MON_SEC="${1:-360}"
PORT="${ASTROLABE_SERIAL_PORT:-}"

if [ -z "$PORT" ]; then
  PORT=$(pio device list 2>/dev/null | awk '/usbmodem/{print $1; exit}' || true)
fi
if [ -z "$PORT" ]; then
  echo "No usbmodem port — plug in the watch." >&2
  exit 1
fi

echo "=== build + upload debug (${PORT}) ==="
pio run -e waveshare_s3_175_debug -t upload --upload-port "$PORT"

echo "=== JTAG → Astrology face ==="
"$ROOT/scripts/jtag_set_face.sh" astro

PY="$ROOT/mcp/astrolabe-esp/.venv/bin/python"
[ -x "$PY" ] || "$ROOT/mcp/astrolabe-esp/setup.sh"

echo "=== serial monitor ${MON_SEC}s (will send: astro) ==="
exec "$PY" -u - "$PORT" "$MON_SEC" <<'PY'
import serial, time, sys
port, mon = sys.argv[1], int(sys.argv[2])
ser = serial.Serial(port, 115200, timeout=0.3)
time.sleep(1.5)
ser.reset_input_buffer()
t0 = time.time()
sent = False
while time.time() - t0 < mon:
    if not sent and time.time() - t0 > 3:
        ser.write(b"astro\n")
        ser.flush()
        print(">> astro", flush=True)
        sent = True
    chunk = ser.read(65536)
    if chunk:
        print(chunk.decode("utf-8", errors="replace"), end="", flush=True)
ser.close()
PY
