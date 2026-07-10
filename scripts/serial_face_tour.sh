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
FACE_RE = re.compile(
    r"^face:\s+([a-z0-9_-]+)\s+enabled=(yes|no)\s+nav=(yes|no)\s+.*?\bported=(yes|no)\b",
    re.I,
)
ACK_RE = re.compile(r"^faces:\s+set\s+([a-z0-9_-]+)\s+ESP_OK\b", re.I)

ser = serial.Serial(port, 115200, timeout=0.25)
try:
    time.sleep(0.35)

    def read_lines(duration):
        deadline = time.monotonic() + duration
        buf = ""
        while time.monotonic() < deadline:
            chunk = ser.read(4096)
            if not chunk:
                time.sleep(0.05)
                continue
            text = chunk.decode("utf-8", errors="replace")
            print(text, end="", flush=True)
            if CRASH.search(text):
                print("serial_face_tour: crash/reset output detected", file=sys.stderr)
                sys.exit(1)
            buf += text
            while "\n" in buf:
                line, buf = buf.split("\n", 1)
                yield line.rstrip("\r")
        if buf:
            yield buf.rstrip("\r")

    ser.reset_input_buffer()
    ser.write(b"faces\n")
    ser.flush()
    faces = []
    for line in read_lines(3.0):
        m = FACE_RE.match(line)
        if m and m.group(2).lower() == "yes" and m.group(3).lower() == "yes" and m.group(4).lower() == "yes":
            faces.append(m.group(1))

    if not faces:
        print("serial_face_tour: no enabled/nav/ported faces found", file=sys.stderr)
        sys.exit(1)

    print(f"tour: start count={len(faces)} dwell_ms={dwell_ms}")
    for idx, slug in enumerate(faces):
        print(f"tour: loading {idx} {slug}", flush=True)
        ser.write(f"faces set {slug}\n".encode("utf-8"))
        ser.flush()
        ack = False
        for line in read_lines(5.0):
            m = ACK_RE.match(line)
            if m and m.group(1).lower() == slug.lower():
                ack = True
                break
        if not ack:
            print(f"serial_face_tour: no ACK for {slug}", file=sys.stderr)
            sys.exit(1)
        time.sleep(max(0, dwell_ms) / 1000.0)

    print("tour: done")
    print("SERIAL_FACE_TOUR PASS")
finally:
    ser.close()
PY
