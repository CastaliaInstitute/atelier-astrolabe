#!/usr/bin/env python3
"""Build/sync the canonical 466px tarot deck into docs assets and SPIFFS seed."""

from __future__ import annotations

import argparse
import shutil
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SOURCE = ROOT / "faculty175" / "storage_seed" / "tarot" / "deck" / "233"
DOCS_DECK = ROOT / "docs" / "assets" / "deck" / "466"
SPIFFS_DECK = ROOT / "faculty175" / "storage_seed" / "tarot" / "deck" / "466"
SIZE = (466, 466)


def convert_png(src: Path, dst: Path, colors: int) -> None:
    im = Image.open(src).convert("RGB")
    if im.size != SIZE:
        im = im.resize(SIZE, Image.Resampling.LANCZOS)
    q = im.quantize(colors=colors, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.FLOYDSTEINBERG)
    dst.parent.mkdir(parents=True, exist_ok=True)
    q.save(dst, optimize=True)


def clear_pngs(path: Path) -> None:
    path.mkdir(parents=True, exist_ok=True)
    for png in path.glob("*.png"):
        png.unlink()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, default=DEFAULT_SOURCE)
    parser.add_argument("--colors", type=int, default=128)
    parser.add_argument("--docs-only", action="store_true")
    args = parser.parse_args()

    source = args.source.resolve()
    files = sorted(source.glob("*.png"))
    if len(files) != 78:
        raise SystemExit(f"expected 78 tarot PNGs in {source}, found {len(files)}")

    clear_pngs(DOCS_DECK)
    for src in files:
        convert_png(src, DOCS_DECK / src.name, args.colors)

    if not args.docs_only:
        if SPIFFS_DECK.exists():
            shutil.rmtree(SPIFFS_DECK)
        shutil.copytree(DOCS_DECK, SPIFFS_DECK)

    docs_bytes = sum(p.stat().st_size for p in DOCS_DECK.glob("*.png"))
    print(f"wrote {len(files)} cards to {DOCS_DECK} ({docs_bytes} bytes)")
    if not args.docs_only:
        spiffs_bytes = sum(p.stat().st_size for p in SPIFFS_DECK.glob("*.png"))
        print(f"synced {SPIFFS_DECK} ({spiffs_bytes} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
