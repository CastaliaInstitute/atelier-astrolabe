#!/usr/bin/env bash
# Capture USB serial log from Astrolabe (115200). Optional: send `astro` to trigger voice.
set -euo pipefail
DURATION="${1:-180}"
TRIGGER="${2:-}"  # pass "astro" to inject test command
PORT="${ASTROLABE_SERIAL_PORT:-${MYNAH_SERIAL_PORT:-}}"

if [ -z "$PORT" ]; then
  for _ in $(seq 1 30); do
    PORT=$(pio device list 2>/dev/null | awk '/usbmodem/{print $1; exit}')
    [ -n "$PORT" ] && break
    sleep 2
  done
fi

if [ -z "$PORT" ]; then
  echo "No /dev/cu.usbmodem* — plug in the watch." >&2
  exit 1
fi

LOG="${ASTROLABE_MONITOR_LOG:-/tmp/astrolabe_monitor.log}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PY="${PY:-$ROOT/mcp/astrolabe-esp/.venv/bin/python}"
if [ ! -x "$PY" ]; then
  PY="${PY_FALLBACK:-/opt/homebrew/bin/python3}"
fi

echo "Port: $PORT  Duration: ${DURATION}s  Log: $LOG  Trigger: ${TRIGGER:-none}"
"$PY" -u - "$PORT" "$DURATION" "$TRIGGER" "$LOG" <<'PY'
import serial, sys, time
port, duration_s, trigger, logpath = sys.argv[1], int(sys.argv[2]), sys.argv[3], sys.argv[4]
ser = serial.Serial(port, 115200, timeout=0.25)
ser.reset_input_buffer()
time.sleep(0.5)
if trigger == "astro":
    ser.write(b"astro\n")
    ser.flush()
    print(">> sent: astro", flush=True)
t0 = time.time()
with open(logpath, "w", encoding="utf-8") as logf:
    logf.write(f"# port={port} started={time.strftime('%Y-%m-%d %H:%M:%S')}\n")
    while time.time() - t0 < duration_s:
        chunk = ser.read(65536)
        if not chunk:
            continue
        text = chunk.decode("utf-8", errors="replace")
        logf.write(text)
        logf.flush()
        for line in text.splitlines():
            line = line.rstrip()
            if line:
                print(line, flush=True)
ser.close()
print(f"# done — full log: {logpath}", flush=True)
PY
