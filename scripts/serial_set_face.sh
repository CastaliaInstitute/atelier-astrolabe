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

# Keep in sync with face_index_from_name() in PocketMynah.ino
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
}

port, face = sys.argv[1], sys.argv[2].strip().lower()
want_idx = None
if face.isdigit():
    want_idx = int(face)
else:
    want_idx = NAME_TO_IDX.get(face)

ser = serial.Serial(port, 115200, timeout=0.3)
try:
    time.sleep(0.25)
    ser.reset_input_buffer()
    ser.write(f"face {face}\n".encode("utf-8"))
    ser.flush()
    buf = ""
    for _ in range(80):
        chunk = ser.read(4096)
        if chunk:
            text = chunk.decode("utf-8", errors="replace")
            print(text, end="", flush=True)
            buf += text
            if "face: usage" in buf:
                print("serial_set_face: firmware rejected face command", file=sys.stderr)
                sys.exit(1)
            m = re.search(r"face:\s*(\d+)", buf)
            if m:
                got = int(m.group(1))
                if want_idx is not None and got != want_idx:
                    print(
                        f"serial_set_face: expected index {want_idx}, got {got}",
                        file=sys.stderr,
                    )
                    sys.exit(1)
                print(f"# face_index={got}", flush=True)
                sys.exit(0)
        time.sleep(0.1)
finally:
    ser.close()

print("serial_set_face: no face: N ack from firmware", file=sys.stderr)
sys.exit(1)
PY
