#!/usr/bin/env python3
"""Bounded USB-JTAG console exchange with astrolabe175c."""
import subprocess
import sys
import time

import serial

PORT = sys.argv[1] if len(sys.argv) > 1 else "/dev/cu.usbmodem114101"
CMD = sys.argv[2] if len(sys.argv) > 2 else "qa status"
WAIT = float(sys.argv[3]) if len(sys.argv) > 3 else 2.0

subprocess.run(["stty", "-f", PORT, "115200"], capture_output=True)

with serial.Serial(PORT, 115200, timeout=0.3) as ser:
    ser.reset_input_buffer()
    ser.write((CMD + "\r\n").encode())
    ser.flush()
    end = time.time() + WAIT
    lines = []
    while time.time() < end:
        data = ser.read(4096)
        if data:
            lines.append(data.decode(errors="replace"))
            end = time.time() + 0.4
    print("".join(lines))
