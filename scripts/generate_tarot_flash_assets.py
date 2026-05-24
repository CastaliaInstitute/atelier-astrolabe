#!/usr/bin/env python3
"""Generate compact flash-resident tarot JPEGs for the Astrolabe watch."""

from __future__ import annotations

import os
from pathlib import Path

from PIL import Image, ImageDraw, ImageEnhance, ImageFilter, ImageOps


ROOT = Path(__file__).resolve().parents[1]
SOURCE_DIR = Path(os.getenv("TAROT_SOURCE_DIR", str(ROOT.parent / "tarot/source/rider-waite/major")))
OUT_DIR = ROOT / "data/tarot"
SIZE = int(os.getenv("TAROT_FLASH_SIZE", "233"))

CARDS = [
    (0, "fool"),
    (1, "magician"),
    (2, "priestess"),
    (3, "empress"),
    (4, "emperor"),
    (5, "hierophant"),
    (6, "lovers"),
    (7, "chariot"),
    (8, "strength"),
    (9, "hermit"),
    (10, "fortune"),
    (11, "justice"),
    (12, "hanged"),
    (13, "death"),
    (14, "temperance"),
    (15, "devil"),
    (16, "tower"),
    (17, "star"),
    (18, "moon"),
    (19, "sun"),
    (20, "judgement"),
    (21, "world"),
]


def source_for(number: int, slug: str) -> Path:
    for ext in ("jpg", "png", "jpeg"):
        path = SOURCE_DIR / f"{number:02d}-{slug}.{ext}"
        if path.exists():
            return path
    raise FileNotFoundError(f"missing source for {number:02d}-{slug} under {SOURCE_DIR}")


def content_crop(img: Image.Image) -> Image.Image:
    w, h = img.size
    # Rider-Waite source cards are portrait scans. These fractions strip the
    # printed border and title strip while keeping the full illustration area.
    box = (
        round(w * 0.075),
        round(h * 0.045),
        round(w * 0.925),
        round(h * 0.805),
    )
    return img.crop(box)


def feather_mask(size: tuple[int, int], feather: int) -> Image.Image:
    w, h = size
    mask = Image.new("L", (w, h), 255)
    px = mask.load()
    for y in range(h):
        dy = min(y, h - 1 - y)
        for x in range(w):
            d = min(x, w - 1 - x, dy)
            if d < feather:
                px[x, y] = min(px[x, y], round(255 * d / max(1, feather)))
    return mask.filter(ImageFilter.GaussianBlur(max(1, feather // 3)))


def round_watch_image(src: Image.Image) -> Image.Image:
    src = ImageOps.exif_transpose(src).convert("RGB")
    scene = content_crop(src)

    bg = ImageOps.fit(scene, (SIZE, SIZE), method=Image.Resampling.LANCZOS, centering=(0.5, 0.45))
    bg = bg.filter(ImageFilter.GaussianBlur(max(4, SIZE // 28)))
    bg = ImageEnhance.Color(bg).enhance(1.16)
    bg = ImageEnhance.Contrast(bg).enhance(0.82)
    bg = ImageEnhance.Brightness(bg).enhance(0.92)

    fg_h = round(SIZE * 0.98)
    fg_w = max(1, round(scene.width * (fg_h / scene.height)))
    fg = scene.resize((fg_w, fg_h), Image.Resampling.LANCZOS)
    fg = ImageEnhance.Color(fg).enhance(1.12)
    fg = ImageEnhance.Contrast(fg).enhance(1.08)
    fg = ImageEnhance.Sharpness(fg).enhance(1.08)

    out = bg.copy()
    out.paste(fg, ((SIZE - fg_w) // 2, (SIZE - fg_h) // 2), feather_mask(fg.size, max(5, SIZE // 28)))

    vignette = Image.new("L", (SIZE, SIZE), 0)
    draw = ImageDraw.Draw(vignette)
    inset = max(2, SIZE // 70)
    draw.ellipse((inset, inset, SIZE - inset - 1, SIZE - inset - 1), fill=255)
    vignette = vignette.filter(ImageFilter.GaussianBlur(max(1, SIZE // 80)))
    edge = Image.new("RGB", (SIZE, SIZE), (18, 14, 18))
    edge.paste(out, (0, 0), vignette)
    out = edge
    return out


def main() -> int:
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    total = 0
    for number, slug in CARDS:
        src_path = source_for(number, slug)
        with Image.open(src_path) as src:
            out = round_watch_image(src)
        dest = OUT_DIR / f"{number:02d}-{slug}.jpg"
        out.save(dest, "JPEG", quality=82, optimize=True, progressive=False, subsampling=1)
        total += dest.stat().st_size
        print(f"{dest.relative_to(ROOT)} {dest.stat().st_size // 1024} KiB")
    print(f"wrote {len(CARDS)} tarot flash assets, {total // 1024} KiB total")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
