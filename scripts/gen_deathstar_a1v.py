#!/usr/bin/env python3
"""Generate the seeded 1-bit Death Star trench-run animation."""

from __future__ import annotations

import math
import struct
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "faculty175" / "storage_seed" / "deathstar.a1v"
MAGIC = b"A1R1"
WIDTH = 466
HEIGHT = 466
FPS = 6
FRAMES = 48
HEADER = struct.Struct("<4sHHHHIII8s")


def put(bits: bytearray, x: int, y: int) -> None:
    if 0 <= x < WIDTH and 0 <= y < HEIGHT:
        bits[y * WIDTH + x] = 1


def line(bits: bytearray, x0: float, y0: float, x1: float, y1: float) -> None:
    steps = max(1, int(max(abs(x1 - x0), abs(y1 - y0))))
    for i in range(steps + 1):
        t = i / steps
        put(bits, round(x0 + (x1 - x0) * t), round(y0 + (y1 - y0) * t))


def circle(bits: bytearray, cx: float, cy: float, r: float) -> None:
    for i in range(max(32, int(r * 6))):
        a = math.tau * i / max(32, int(r * 6))
        put(bits, round(cx + math.cos(a) * r), round(cy + math.sin(a) * r))


def write_varint(out: bytearray, value: int) -> None:
    while value >= 0x80:
        out.append((value & 0x7F) | 0x80)
        value >>= 7
    out.append(value)


def encode(bits: bytearray) -> bytes:
    out = bytearray([bits[0]])
    color = bits[0]
    run = 0
    for bit in bits:
        if bit == color:
            run += 1
        else:
            write_varint(out, run)
            color = bit
            run = 1
    write_varint(out, run)
    return bytes(out)


def frame(n: int) -> bytes:
    bits = bytearray(WIDTH * HEIGHT)
    cx = WIDTH / 2
    cy = HEIGHT / 2
    phase = n / FRAMES
    van_x = cx + 18 * math.sin(phase * math.tau * 1.7)
    van_y = 94 + 12 * math.sin(phase * math.tau)

    for i in range(8):
        x = -80 + i * 70 + (n * 9) % 70
        line(bits, x, HEIGHT - 1, van_x, van_y)
        line(bits, WIDTH - x, HEIGHT - 1, van_x, van_y)
    for i in range(10):
        y = HEIGHT - 18 - ((i * 58 + n * 13) % 370)
        scale = max(0.05, (HEIGHT - y) / HEIGHT)
        half = 24 + scale * 232
        line(bits, cx - half, y, cx + half, y)
        if i % 3 == 0:
            line(bits, cx - half * 0.58, y, cx - half * 0.34, y - 18)
            line(bits, cx + half * 0.58, y, cx + half * 0.34, y - 18)

    disk_r = 62 + 10 * math.sin(phase * math.tau * 1.4)
    disk_x = 342 + 22 * math.sin(phase * math.tau * 0.7)
    disk_y = 140 + 12 * math.cos(phase * math.tau * 0.9)
    circle(bits, disk_x, disk_y, disk_r)
    circle(bits, disk_x - 18, disk_y - 14, disk_r * 0.22)
    line(bits, disk_x - disk_r * 0.72, disk_y + disk_r * 0.18, disk_x + disk_r * 0.7, disk_y - disk_r * 0.1)
    for k in range(4):
        a = -0.8 + k * 0.28
        line(bits, disk_x + math.cos(a) * disk_r * 0.3, disk_y + math.sin(a) * disk_r * 0.3,
             disk_x + math.cos(a) * disk_r, disk_y + math.sin(a) * disk_r)

    if n % 2 == 0:
        circle(bits, cx, cy, 78)
    circle(bits, cx, cy, 15)
    line(bits, cx - 104, cy, cx - 24, cy)
    line(bits, cx + 24, cy, cx + 104, cy)
    line(bits, cx, cy - 104, cx, cy - 24)
    line(bits, cx, cy + 24, cx, cy + 104)

    laser_y = cy + 126 - ((n % 24) / 24) * 252
    line(bits, cx - 34, HEIGHT - 1, cx - 5, laser_y)
    line(bits, cx + 34, HEIGHT - 1, cx + 5, laser_y)

    for i in range(26):
        x = int((i * 97 + n * (3 + i % 5)) % WIDTH)
        y = int((i * 53 + n * (2 + i % 7)) % HEIGHT)
        if abs(x - disk_x) > disk_r or abs(y - disk_y) > disk_r:
            put(bits, x, y)

    return encode(bits)


def main() -> int:
    frames = [frame(i) for i in range(FRAMES)]
    index_offset = HEADER.size
    data_offset = index_offset + len(frames) * 4
    offsets = []
    cursor = data_offset
    for payload in frames:
        offsets.append(cursor)
        cursor += len(payload)

    OUT.parent.mkdir(parents=True, exist_ok=True)
    with OUT.open("wb") as f:
        f.write(HEADER.pack(MAGIC, WIDTH, HEIGHT, FPS, 0, len(frames), index_offset, data_offset, b"\0" * 8))
        for offset in offsets:
            f.write(struct.pack("<I", offset))
        for payload in frames:
            f.write(payload)
    print(f"wrote {OUT} ({OUT.stat().st_size} bytes, {FRAMES} frames @ {FPS} fps)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
