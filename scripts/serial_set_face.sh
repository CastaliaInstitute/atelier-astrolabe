#!/usr/bin/env bash
# Send `face <name|index>` over serial without DTR reset (device must already be running).
#
#   ./scripts/serial_set_face.sh /dev/cu.usbmodem1101 moon
#
# Exits 0 when firmware prints "face: N". Exits 1 on timeout or usage error.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PORT="${1:?serial port}"
FACE="${2:?face name or index}"

VENV_PY="${ROOT}/mcp/astrolabe-esp/.venv/bin/python"
if [[ ! -x "$VENV_PY" ]]; then
  "${ROOT}/mcp/astrolabe-esp/setup.sh"
fi

exec "$VENV_PY" -u - "$PORT" "$FACE" <<'PY'
import re
import sys
import time

import serial

# Keep in sync with pm_faces_index_from_name() in faces/pm_clock.cpp
NAME_TO_IDX = {
    "classic": 0,
    "hue": 0,
    "analog": 0,
    "apocalypso": 1,
    "digital": 2,
    "spotify": 3,
    "astro": 4,
    "astrology": 4,
    "moon": 5,
    "calcifer": 6,
    "schedule": 6,
    "castalia": 7,
    "syn": 8,
    "synastry": 8,
    "spectrum": 9,
    "fft": 9,
    "audio": 9,
    "sound": 9,
    "chakra": 10,
    "bowl": 11,
    "tibetan": 11,
    "tibetan_bowl": 11,
    "rocket": 12,
    "launch": 12,
    "launchclock": 12,
    "radar": 13,
    "presence": 13,
    "peers": 13,
    "faculty": 14,
    "fac": 14,
    "weather": 15,
}

port, face = sys.argv[1], sys.argv[2].strip().lower()
want_idx = None
if face.isdigit():
    want_idx = int(face)
else:
    want_idx = NAME_TO_IDX.get(face)

ser = serial.Serial(port, 115200, timeout=0.3)
try:
    time.sleep(0.35)
    # Drain boot/log spam so the face ack is not lost in a full RX buffer.
    drain_end = time.monotonic() + 0.4
    while time.monotonic() < drain_end:
        chunk = ser.read(4096)
        if not chunk:
            time.sleep(0.05)
    ser.reset_input_buffer()
    ser.write(f"face {face}\r\n".encode("utf-8"))
    ser.flush()
    buf = ""
    for _ in range(120):
        chunk = ser.read(4096)
        if chunk:
            text = chunk.decode("utf-8", errors="replace")
            print(text, end="", flush=True)
            buf += text
            if len(buf) > 65536:
                buf = buf[-32768:]
            if "face: usage" in buf:
                print("serial_set_face: firmware rejected face command", file=sys.stderr)
                sys.exit(1)
            for m in re.finditer(r"face:\s*(\d+)", buf):
                got = int(m.group(1))
                if want_idx is not None and got != want_idx:
                    continue
                print(f"# face_index={got}", flush=True)
                sys.exit(0)
        time.sleep(0.12)
finally:
    ser.close()

print("serial_set_face: no face: N ack from firmware", file=sys.stderr)
sys.exit(1)
PY
