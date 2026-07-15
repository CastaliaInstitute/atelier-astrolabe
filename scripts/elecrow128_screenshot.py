#!/usr/bin/env python3
"""Capture an Elecrow128 framebuffer screenshot over USB serial."""

from __future__ import annotations

import argparse
import os
import sys
import time
from pathlib import Path

import serial


WIDTH = 240
HEIGHT = 240
RGB_BYTES = WIDTH * HEIGHT * 3
BEGIN = b"ASTRO_SHOT_BEGIN "


def read_shot(port: str, timeout_s: float) -> bytes:
    ser = serial.Serial(port, 115200, timeout=0.02, write_timeout=1, dsrdtr=False, rtscts=False)
    ser.dtr = False
    ser.rts = False

    buf = bytearray()
    start = time.monotonic()
    writes = 0
    while time.monotonic() - start < timeout_s and len(buf) < 700_000:
        elapsed = time.monotonic() - start
        if writes == 0 and b"ASTRO_CMD_READY" in buf:
            time.sleep(0.2)
            ser.write(b"shot\n")
            ser.flush()
            writes += 1
        if writes == 0 and elapsed > 5.0:
            ser.write(b"shot\n")
            ser.flush()
            writes += 1
        if writes == 1 and BEGIN not in buf and elapsed > 10.0:
            ser.write(b"shot\n")
            ser.flush()
            writes += 1

        data = ser.read(8192)
        if data:
            buf.extend(data)

        begin = buf.find(BEGIN)
        if begin < 0:
            continue
        header_end = buf.find(b"\n", begin)
        if header_end < 0:
            continue
        payload_start = header_end + 1
        payload_end = payload_start + RGB_BYTES
        if len(buf) >= payload_end:
            return b"P6\n240 240\n255\n" + bytes(buf[payload_start:payload_end])

    Path("/tmp/elecrow128_screenshot_failed.bin").write_bytes(buf)
    raise TimeoutError("no complete ASTRO_SHOT_BEGIN payload received")


def write_image(ppm: bytes, output: Path) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    if output.suffix.lower() == ".png":
        from PIL import Image

        tmp = output.with_suffix(".ppm")
        tmp.write_bytes(ppm)
        Image.open(tmp).save(output)
        tmp.unlink(missing_ok=True)
    else:
        output.write_bytes(ppm)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("-p", "--port", default="/dev/ttyACM1")
    parser.add_argument("-o", "--output", default="artifacts/elecrow128/psychometer-screenshot.png")
    parser.add_argument("--timeout", type=float, default=45.0)
    args = parser.parse_args()

    ppm = read_shot(args.port, args.timeout)
    write_image(ppm, Path(args.output))
    print(args.output)
    sys.stdout.flush()
    os._exit(0)


if __name__ == "__main__":
    raise SystemExit(main())
