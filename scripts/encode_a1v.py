#!/usr/bin/env python3
"""Encode a video as Astrolabe 1-bit streaming video (.a1v)."""

from __future__ import annotations

import argparse
import struct
import subprocess
from pathlib import Path


MAGIC = b"A1R1"
WIDTH = 466
HEIGHT = 466
HEADER = struct.Struct("<4sHHHHIII8s")


def bits_from_gray(frame: bytes, threshold: int) -> bytearray:
    return bytearray(1 if px >= threshold else 0 for px in frame)


def write_varint(out: bytearray, value: int) -> None:
    while value >= 0x80:
        out.append((value & 0x7F) | 0x80)
        value >>= 7
    out.append(value)


def encode_frame(bits: bytes | bytearray) -> bytes:
    out = bytearray()
    color = bits[0]
    out.append(color)
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


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--fps", type=int, default=8)
    parser.add_argument("--threshold", type=int, default=128)
    parser.add_argument("--scale-width", type=int, default=828)
    args = parser.parse_args()

    if args.fps <= 0 or args.fps > 60:
        parser.error("--fps must be in 1..60")
    if args.threshold < 0 or args.threshold > 255:
        parser.error("--threshold must be in 0..255")

    vf = (
        f"fps={args.fps},"
        f"scale={args.scale_width}:{HEIGHT}:flags=lanczos,"
        f"crop={WIDTH}:{HEIGHT},format=gray"
    )
    cmd = [
        "ffmpeg",
        "-v",
        "error",
        "-i",
        str(args.input),
        "-vf",
        vf,
        "-an",
        "-f",
        "rawvideo",
        "-pix_fmt",
        "gray",
        "-",
    ]

    frame_size = WIDTH * HEIGHT
    offsets: list[int] = []
    frames: list[bytes] = []
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE)
    assert proc.stdout is not None
    try:
        while True:
            frame = proc.stdout.read(frame_size)
            if not frame:
                break
            if len(frame) != frame_size:
                raise RuntimeError(f"partial raw frame: {len(frame)} bytes")
            frames.append(encode_frame(bits_from_gray(frame, args.threshold)))
    finally:
        proc.stdout.close()
    rc = proc.wait()
    if rc != 0:
        raise RuntimeError(f"ffmpeg exited with {rc}")
    if not frames:
        raise RuntimeError("no frames encoded")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    header_size = HEADER.size
    index_offset = header_size
    data_offset = index_offset + len(frames) * 4
    cursor = data_offset
    for frame in frames:
        offsets.append(cursor)
        cursor += len(frame)

    with args.output.open("wb") as f:
        f.write(
            HEADER.pack(
                MAGIC,
                WIDTH,
                HEIGHT,
                args.fps,
                0,
                len(frames),
                index_offset,
                data_offset,
                b"\0" * 8,
            )
        )
        for offset in offsets:
            f.write(struct.pack("<I", offset))
        for frame in frames:
            f.write(frame)

    print(f"encoded {len(frames)} frames @ {args.fps} fps -> {args.output}")
    print(f"size {args.output.stat().st_size / (1024 * 1024):.2f} MiB")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
