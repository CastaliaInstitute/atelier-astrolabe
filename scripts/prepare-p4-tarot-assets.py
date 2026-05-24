#!/usr/bin/env python3
"""Copy generated 720px Major Arcana art into the P4 SD-card layout.

Source files come from the sibling tarot repo's generated round deck:
  ../tarot/docs/assets/deck/720/major-00-fool.png

The firmware reads:
  /sdcard/astrolabe/tarot/720/major-00-fool.png
"""

from __future__ import annotations

import argparse
import shutil
from pathlib import Path


CARDS = [
    "fool",
    "magician",
    "priestess",
    "empress",
    "emperor",
    "hierophant",
    "lovers",
    "chariot",
    "strength",
    "hermit",
    "fortune",
    "justice",
    "hanged",
    "death",
    "temperance",
    "devil",
    "tower",
    "star",
    "moon",
    "sun",
    "judgement",
    "world",
]

def main() -> int:
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--source",
        type=Path,
        default=root.parent / "tarot" / "docs" / "assets" / "deck" / "720",
        help="Directory containing major-00-fool.png style 720px generated art.",
    )
    parser.add_argument(
        "destination",
        type=Path,
        help="Mounted SD-card root or the final astrolabe/tarot/720 directory.",
    )
    args = parser.parse_args()

    source = args.source.expanduser().resolve()
    destination = args.destination.expanduser().resolve()
    if destination.name != "720":
        destination = destination / "astrolabe" / "tarot" / "720"

    if not source.is_dir():
        raise FileNotFoundError(source)
    destination.mkdir(parents=True, exist_ok=True)

    copied = 0
    for number, slug in enumerate(CARDS):
        name = f"major-{number:02d}-{slug}.png"
        src = source / name
        if not src.exists():
            raise FileNotFoundError(src)
        shutil.copy2(src, destination / name)
        copied += 1

    print(f"copied {copied} tarot assets to {destination}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
