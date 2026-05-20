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

# Keep in sync with face_index_from_name() in sketches/Astrolabe/Astrolabe.ino.
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
    "settings": 8,
    "wifi": 8,
    "syn": 9,
    "synastry": 9,
    "spectrum": 10,
    "fft": 10,
    "audio": 10,
    "sound": 10,
    "chakra": 11,
    "bowl": 12,
    "tibetan": 12,
    "tibetan_bowl": 12,
    "rocket": 13,
    "launch": 13,
    "launchclock": 13,
    "radar": 14,
    "presence": 14,
    "peers": 14,
    "locator": 14,
    "locations": 14,
    "faculty": 15,
    "fac": 15,
    "weather": 16,
    "quotes": 17,
    "quote": 17,
    "qotd": 17,
    "transits": 18,
    "live_transits": 18,
    "live-transits": 18,
    "live": 18,
    "tarot": 19,
    "cards": 19,
    "card": 19,
    "arcana": 19,
    "notes": 20,
    "note": 20,
    "ocarina": 21,
    "ocarina_face": 21,
    "flute": 21,
    "bongo": 22,
    "drum": 22,
    "drums": 22,
    "conga": 22,
    "piano": 23,
    "keys": 23,
    "keyboard": 23,
    "level": 24,
    "bubble": 24,
    "bubble_level": 24,
    "imu": 24,
    "pandrum": 25,
    "pan_drum": 25,
    "pan-drum": 25,
    "pandrom": 25,
    "pandrom_face": 25,
    "handpan": 25,
    "hang": 25,
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
