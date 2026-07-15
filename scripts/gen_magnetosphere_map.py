#!/usr/bin/env python3
"""Generate a 466x466 SPIFFS seed map for the LVGL magnetosphere face."""

from __future__ import annotations

import math
import struct
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter

SIZE = 466
ROOT = Path(__file__).resolve().parents[1]
OUT_PNG = ROOT / "astrolabe175c" / "storage_seed" / "space" / "magnetosphere_466.png"
OUT_RGB565 = ROOT / "astrolabe175c" / "storage_seed" / "space" / "magnetosphere_466.rgb565"


def rgb565(r: int, g: int, b: int) -> int:
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def blend(dst: tuple[int, int, int], src: tuple[int, int, int], alpha: float) -> tuple[int, int, int]:
    ia = 1.0 - alpha
    return (
        int(dst[0] * ia + src[0] * alpha + 0.5),
        int(dst[1] * ia + src[1] * alpha + 0.5),
        int(dst[2] * ia + src[2] * alpha + 0.5),
    )


def make_background() -> Image.Image:
    pixels: list[tuple[int, int, int]] = []
    cx = SIZE / 2.0
    cy = SIZE / 2.0
    for y in range(SIZE):
        for x in range(SIZE):
            dx = (x - cx) / cx
            dy = (y - cy) / cy
            r = min(1.0, math.sqrt(dx * dx + dy * dy))
            v = max(0.0, 1.0 - r)
            solar = max(0.0, 1.0 - x / SIZE)
            tail = max(0.0, x / SIZE)
            base = (
                int(2 + 8 * v + 20 * solar),
                int(6 + 18 * v + 20 * solar),
                int(18 + 38 * v + 36 * tail),
            )
            pixels.append(base)
    im = Image.new("RGB", (SIZE, SIZE))
    im.putdata(pixels)
    return im


def draw_field_line(draw: ImageDraw.ImageDraw, yoff: float, color: tuple[int, int, int], width: int) -> None:
    cx = SIZE / 2.0
    cy = SIZE / 2.0
    pts: list[tuple[float, float]] = []
    for i in range(96):
        t = i / 95.0
        x = cx - 138 + t * 330
        pinch = 1.0 - 0.52 * math.exp(-((t - 0.43) ** 2) / 0.045)
        y = cy + yoff * pinch + math.sin(t * math.pi * 2.0) * 7.0
        pts.append((x, y))
    draw.line(pts, fill=color, width=width, joint="curve")


def make_map() -> Image.Image:
    im = make_background()
    glow = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    gd = ImageDraw.Draw(glow, "RGBA")

    # Solar wind pressure arriving from the left.
    for i in range(14):
        y = 64 + i * 28
        gd.line([(0, y), (182, y + math.sin(i * 0.9) * 18)], fill=(255, 188, 90, 44), width=3)
    gd.ellipse((-22, 58, 80, 160), fill=(255, 182, 72, 170))
    gd.ellipse((-54, 28, 114, 190), outline=(255, 164, 74, 72), width=8)

    # Bow shock and magnetopause.
    gd.arc((90, 52, 330, 414), 98, 262, fill=(126, 220, 255, 220), width=5)
    gd.arc((116, 78, 332, 388), 104, 256, fill=(72, 246, 216, 180), width=4)
    gd.arc((62, 20, 354, 446), 94, 266, fill=(130, 112, 255, 90), width=3)

    # Magnetotail lobes.
    gd.polygon([(238, 150), (466, 68), (466, 198), (286, 218)], fill=(60, 178, 255, 44))
    gd.polygon([(238, 316), (466, 398), (466, 268), (286, 248)], fill=(148, 100, 255, 48))
    for yoff, color in [(-142, (88, 242, 214)), (-104, (112, 220, 255)), (-66, (156, 128, 255)),
                        (66, (156, 128, 255)), (104, (112, 220, 255)), (142, (88, 242, 214))]:
        draw_field_line(gd, yoff, (*color, 168), 3)

    glow = glow.filter(ImageFilter.GaussianBlur(1.0))
    im = Image.alpha_composite(im.convert("RGBA"), glow)

    detail = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    dd = ImageDraw.Draw(detail, "RGBA")
    for yoff, color in [(-142, (156, 255, 238)), (-104, (160, 232, 255)), (-66, (196, 174, 255)),
                        (66, (196, 174, 255)), (104, (160, 232, 255)), (142, (156, 255, 238))]:
        draw_field_line(dd, yoff, (*color, 210), 2)
    dd.ellipse((202, 202, 264, 264), fill=(70, 164, 255, 255), outline=(206, 246, 255, 210), width=2)
    dd.arc((184, 184, 282, 282), 212, 334, fill=(96, 226, 160, 220), width=5)
    dd.arc((184, 184, 282, 282), 34, 154, fill=(96, 226, 160, 220), width=5)
    dd.ellipse((218, 218, 246, 246), outline=(246, 255, 220, 170), width=2)

    # Sparse charged-particle points, deterministic.
    for i in range(80):
        x = (41 * i + 17) % SIZE
        y = (73 * i + 91) % SIZE
        if x > 188 or i % 4 == 0:
            color = (255, 214, 116, 120) if i % 3 == 0 else (128, 226, 255, 108)
            dd.ellipse((x - 1, y - 1, x + 1, y + 1), fill=color)

    return Image.alpha_composite(im, detail).convert("RGB")


def write_rgb565(path: Path, im: Image.Image) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    data = bytearray()
    for r, g, b in im.getdata():
        data.extend(struct.pack("<H", rgb565(r, g, b)))
    path.write_bytes(data)


def main() -> None:
    im = make_map()
    OUT_PNG.parent.mkdir(parents=True, exist_ok=True)
    im.save(OUT_PNG)
    write_rgb565(OUT_RGB565, im)
    print(f"wrote {OUT_PNG}")
    print(f"wrote {OUT_RGB565}")


if __name__ == "__main__":
    main()
