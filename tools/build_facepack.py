#!/usr/bin/env python3
"""Build a LittleFS image for faces_a / faces_b from facepacks/<name>/."""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def find_mklittlefs() -> str | None:
    for candidate in (
        shutil.which("mklittlefs"),
        Path.home() / ".platformio/packages/tool-mklittlefs/mklittlefs",
    ):
        if candidate and Path(candidate).exists():
            return str(candidate)
    return shutil.which("mklittlefs")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--input", type=Path, default=ROOT / "facepacks/core")
    ap.add_argument("--output", type=Path, default=ROOT / "build/core-facepack.img")
    ap.add_argument("--size", type=lambda x: int(x, 0), default=0x700000)
    ap.add_argument("--version", default="0.4.2")
    args = ap.parse_args()

    mklittlefs = find_mklittlefs()
    if not mklittlefs:
        print("mklittlefs not found; install PlatformIO tool-mklittlefs or add to PATH", file=sys.stderr)
        return 1

    src = args.input.resolve()
    if not src.is_dir():
        print(f"missing input dir: {src}", file=sys.stderr)
        return 1

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as td:
        staging = Path(td) / "staging"
        shutil.copytree(src, staging)
        manifest = staging / "manifest.json"
        if manifest.exists():
            text = manifest.read_text()
            text = text.replace('"0.4.2"', f'"{args.version}"', 1)
            manifest.write_text(text)

        cmd = [
            mklittlefs,
            "-c",
            str(staging),
            "-s",
            str(args.size),
            "-p",
            "256",
            "-b",
            "4096",
            args.output.as_posix(),
        ]
        print(" ".join(cmd))
        subprocess.check_call(cmd)

    print(f"wrote {args.output} ({args.output.stat().st_size} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
