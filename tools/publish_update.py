#!/usr/bin/env python3
"""Publish runtime .bin and face-pack .img to the configured update bucket layout (stub)."""

from __future__ import annotations

import argparse
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--channel", default="dev")
    ap.add_argument("--runtime-bin", type=Path)
    ap.add_argument("--facepack-img", type=Path)
    ap.add_argument("--version", required=True)
    ap.add_argument("--dest", type=Path, default=ROOT / "dist/updates")
    args = ap.parse_args()

    base = args.dest / "mynah" / args.channel / args.version
    base.mkdir(parents=True, exist_ok=True)
    if args.runtime_bin:
        shutil.copy2(args.runtime_bin, base / "firmware.bin")
    if args.facepack_img:
        shutil.copy2(args.facepack_img, base / "faces.img")
    print(f"staged under {base}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
