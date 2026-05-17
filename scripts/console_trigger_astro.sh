#!/usr/bin/env bash
# Send serial `astro` to trigger astrology BOOT voice; capture logs for 120s.
set -euo pipefail
PORT="${ASTROLABE_SERIAL_PORT:-${MYNAH_SERIAL_PORT:-/dev/cu.usbmodem1101}}"
PY="${PY:-/opt/homebrew/bin/python3.11}"
"$PY" -u - "$PORT" <<'PY'
import serial, sys, time
port = sys.argv[1]
ser = serial.Serial(port, 115200, timeout=0.2)
ser.reset_input_buffer()
time.sleep(0.3)
ser.write(b"astro\n")
ser.flush()
print(f"sent astro to {port}", flush=True)
t0 = time.time()
while time.time() - t0 < 400:
    chunk = ser.read(16384)
    if not chunk:
        continue
    text = chunk.decode("utf-8", errors="replace")
    for line in text.splitlines():
        line = line.strip()
        if line:
            print(line, flush=True)
ser.close()
PY
