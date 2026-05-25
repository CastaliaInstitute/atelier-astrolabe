#!/usr/bin/env python3
"""Prepare full-resolution tarot deck art for the P4 SD-card layout.

Source files come from the sibling tarot repo's generated round deck masters:
  ../tarot/docs/assets/deck/720/major-00-fool.png
  ../tarot/docs/assets/deck/720/wands-01-ace-wands.png

The firmware reads:
  /sdcard/astrolabe/tarot/720/major-00-fool.png
  /sdcard/astrolabe/tarot/720/wands-01-ace-wands.png
"""

from __future__ import annotations

import argparse
import shutil
from pathlib import Path

from PIL import Image


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--source",
        type=Path,
        default=root.parent / "tarot" / "docs" / "assets" / "deck" / "720",
        help="Directory containing generated deck PNGs.",
    )
    parser.add_argument(
        "--size",
        type=int,
        default=720,
        help="Output square size in pixels. The P4 4C display uses 720.",
    )
    parser.add_argument(
        "destination",
        type=Path,
        help="Mounted SD-card root or the final astrolabe/tarot/720 directory.",
    )
    args = parser.parse_args()

    source = args.source.expanduser().resolve()
    destination = args.destination.expanduser().resolve()
    output_size = int(args.size)
    if output_size <= 0:
        raise ValueError("--size must be positive")
    if destination.name != str(output_size):
        destination = destination / "astrolabe" / "tarot" / str(output_size)

    if not source.is_dir():
        raise FileNotFoundError(source)
    destination.mkdir(parents=True, exist_ok=True)

    copied = 0
    for src in sorted(source.glob("*.png")):
        name = src.name
        src = source / name
        dst = destination / name
        with Image.open(src) as image:
            image = image.convert("RGBA")
            if image.size == (output_size, output_size):
                shutil.copy2(src, dst)
            else:
                image = image.resize((output_size, output_size), Image.Resampling.LANCZOS)
                image.save(dst, optimize=True)
        copied += 1

    print(f"prepared {copied} tarot assets at {output_size}px from {source} to {destination}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
