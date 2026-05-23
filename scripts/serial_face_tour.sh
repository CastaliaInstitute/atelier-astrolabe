#!/usr/bin/env bash
# Run the firmware face tour over serial and fail on crash/reset output.
#
#   ./scripts/serial_face_tour.sh /dev/cu.usbmodem1101
#   ./scripts/serial_face_tour.sh /dev/cu.usbmodem1101 1800
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PORT="${1:?serial port}"
DWELL_MS="${2:-2800}"

VENV_PY="${ROOT}/mcp/astrolabe-esp/.venv/bin/python"
if [[ ! -x "$VENV_PY" ]]; then
  "${ROOT}/mcp/astrolabe-esp/setup.sh"
fi

exec "$VENV_PY" -u - "$PORT" "$DWELL_MS" <<'PY'
import re
import sys
import time

import serial

CRASH = re.compile(
    r"Guru Meditation|Backtrace:|Stack canary|stack overflow|panic|Brownout|ASTROLABE_ALERT",
    re.I,
)

port = sys.argv[1]
dwell_ms = int(sys.argv[2])
face_count = 40
timeout = max(30.0, (face_count * dwell_ms / 1000.0) + 18.0)

ser = serial.Serial(port, 115200, timeout=0.25)
try:
    time.sleep(0.35)
    ser.reset_input_buffer()
    ser.write(f"tour {dwell_ms}\n".encode("utf-8"))
    ser.flush()
    seen = set()
    started = False
    deadline = time.monotonic() + timeout
    buf = ""
    while time.monotonic() < deadline:
        chunk = ser.read(4096)
        if not chunk:
            time.sleep(0.05)
            continue
        text = chunk.decode("utf-8", errors="replace")
        print(text, end="", flush=True)
        buf += text
        if CRASH.search(text):
            print("serial_face_tour: crash/reset output detected", file=sys.stderr)
            sys.exit(1)
        while "\n" in buf:
            line, buf = buf.split("\n", 1)
            line = line.rstrip("\r")
            if line.startswith("tour: start"):
                started = True
            m = re.search(r"tour: loading\s+(\d+)\s+([a-z0-9_ -]+)", line)
            if m:
                seen.add(int(m.group(1)))
            if line == "tour: done":
                missing = [i for i in range(face_count) if i not in seen]
                if not started:
                    print("serial_face_tour: tour did not start", file=sys.stderr)
                    sys.exit(1)
                if missing:
                    print(f"serial_face_tour: missing faces {missing}", file=sys.stderr)
                    sys.exit(1)
                print("SERIAL_FACE_TOUR PASS")
                sys.exit(0)
finally:
    ser.close()

print("serial_face_tour: timed out waiting for tour: done", file=sys.stderr)
sys.exit(1)
PY
