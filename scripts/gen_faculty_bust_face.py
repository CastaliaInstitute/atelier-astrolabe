#!/usr/bin/env python3
"""Generate a full-screen RGB565 Faculty face seed asset from a bust PNG."""

from __future__ import annotations

import math
import struct
from pathlib import Path

from PIL import Image, ImageEnhance

SIZE = 466
ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "faculty175" / "storage_seed" / "v4-a.einstein-right.png"
OUT_PNG = ROOT / "faculty175" / "storage_seed" / "faculty" / "einstein_466.png"
OUT_RGB565 = ROOT / "faculty175" / "storage_seed" / "faculty" / "einstein_466.rgb565"


def rgb565(r: int, g: int, b: int) -> int:
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def make_background() -> Image.Image:
    pixels: list[tuple[int, int, int]] = []
    cx = SIZE / 2.0
    cy = SIZE / 2.0
    for y in range(SIZE):
        for x in range(SIZE):
            dx = (x - cx) / cx
            dy = (y - cy) / cy
            r = min(1.0, math.sqrt(dx * dx + dy * dy))
            top = 24 + int(18 * max(0.0, 1.0 - y / SIZE))
            warm = int(18 * max(0.0, 1.0 - r))
            pixels.append((top + warm, top + warm, top + 6 + warm))
    im = Image.new("RGB", (SIZE, SIZE))
    im.putdata(pixels)
    return im


def circular_alpha(w: int, h: int, feather: float = 0.06) -> Image.Image:
    alpha = Image.new("L", (w, h), 0)
    px = alpha.load()
    cx = w / 2.0
    cy = h / 2.0
    radius = min(w, h) * 0.51
    for y in range(h):
        for x in range(w):
            d = math.sqrt((x - cx) ** 2 + (y - cy) ** 2)
            t = max(0.0, min(1.0, (radius - d) / (radius * feather)))
            px[x, y] = int(255 * t + 0.5)
    return alpha


def make_face() -> Image.Image:
    bg = make_background().convert("RGBA")
    src = Image.open(SOURCE).convert("RGB")
    src = ImageEnhance.Contrast(src).enhance(1.08)
    src = ImageEnhance.Sharpness(src).enhance(1.18)

    bust_size = 376
    bust = src.resize((bust_size, bust_size), Image.Resampling.LANCZOS).convert("RGBA")
    bust.putalpha(circular_alpha(bust_size, bust_size))

    # Tint the grayscale bust slightly toward cool ivory so it harmonizes with the watch face.
    tinted = Image.new("RGBA", bust.size)
    for i, (r, g, b, a) in enumerate(bust.getdata()):
        v = int((r * 0.35 + g * 0.4 + b * 0.25) + 0.5)
        tinted.putpixel((i % bust_size, i // bust_size), (min(255, v + 18), min(255, v + 20), min(255, v + 24), a))

    bg.alpha_composite(tinted, ((SIZE - bust_size) // 2, 52))

    # Keep the lower area calm for the small status line.
    pixels = []
    for y, (r, g, b, a) in enumerate(bg.getdata()):
        yy = y // SIZE
        fade = max(0.0, min(1.0, (yy - 328) / 138.0))
        pixels.append((int(r * (1.0 - 0.42 * fade)), int(g * (1.0 - 0.42 * fade)), int(b * (1.0 - 0.42 * fade)), a))
    bg.putdata(pixels)
    return bg.convert("RGB")


def write_rgb565(path: Path, im: Image.Image) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    data = bytearray()
    for r, g, b in im.getdata():
        data.extend(struct.pack("<H", rgb565(r, g, b)))
    path.write_bytes(data)


def main() -> None:
    im = make_face()
    OUT_PNG.parent.mkdir(parents=True, exist_ok=True)
    im.save(OUT_PNG)
    write_rgb565(OUT_RGB565, im)
    print(f"wrote {OUT_PNG}")
    print(f"wrote {OUT_RGB565}")


if __name__ == "__main__":
    main()
