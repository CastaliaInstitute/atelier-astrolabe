#!/usr/bin/env python3
"""Build moon_texture.h — 8-bit greyscale moon (PROGMEM), circular mask in pm_moon_draw."""
from __future__ import annotations

import argparse
import math
import sys
import urllib.request
from pathlib import Path

try:
    from PIL import Image, ImageEnhance
except ImportError:
    print("pip install pillow", file=sys.stderr)
    raise

DEFAULT_URL = "https://svs.gsfc.nasa.gov/vis/a000000/a004700/a004720/lroc_color_poles_4k.tif"
ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUT = ROOT / "sketches" / "Astrolabe" / "faces" / "moon" / "moon_texture.h"


def load_source(path: Path | None, url: str) -> Image.Image:
    if path and path.is_file():
        im = Image.open(path).convert("RGB")
    else:
        print(f"Downloading {url} …")
        req = urllib.request.Request(url, headers={"User-Agent": "CastaliaInstitute-astrolabe/1.0"})
        with urllib.request.urlopen(req, timeout=120) as resp:
            data = resp.read()
        from io import BytesIO

        im = Image.open(BytesIO(data)).convert("RGB")
    return im


def sample_bilinear(gray: Image.Image, x: float, y: float) -> int:
    w, h = gray.size
    x %= w
    y = max(0.0, min(float(h - 1), y))
    x0 = int(math.floor(x))
    y0 = int(math.floor(y))
    x1 = (x0 + 1) % w
    y1 = min(h - 1, y0 + 1)
    fx = x - x0
    fy = y - y0
    p00 = gray.getpixel((x0, y0))
    p10 = gray.getpixel((x1, y0))
    p01 = gray.getpixel((x0, y1))
    p11 = gray.getpixel((x1, y1))
    top = p00 * (1.0 - fx) + p10 * fx
    bottom = p01 * (1.0 - fx) + p11 * fx
    return int(round(top * (1.0 - fy) + bottom * fy))


def render_equirect_nearside(im: Image.Image, size: int) -> Image.Image:
    """Render a full-frame orthographic near-side moon from an equirectangular map."""
    gray_src = ImageEnhance.Contrast(im.convert("L")).enhance(1.08)
    gray_src = ImageEnhance.Brightness(gray_src).enhance(0.96)
    w, h = gray_src.size
    out = Image.new("L", (size, size), 0)
    px = out.load()
    center = (size - 1) / 2.0
    radius = center
    for y in range(size):
        yy = (center - y) / radius
        for x in range(size):
            xx = (x - center) / radius
            d2 = xx * xx + yy * yy
            if d2 > 1.0:
                continue
            zz = math.sqrt(max(0.0, 1.0 - d2))
            lon = math.atan2(xx, zz)
            lat = math.asin(yy)
            u = (lon + math.pi) / (2.0 * math.pi) * w
            v = (math.pi / 2.0 - lat) / math.pi * h
            px[x, y] = sample_bilinear(gray_src, u, v)
    return out


def crop_photo_disk(im: Image.Image, size: int) -> Image.Image:
    """Crop to the actual lunar disk and resize for full-frame photo sources."""
    gray_src = im.convert("L")
    bbox = gray_src.point(lambda p: 255 if p > 8 else 0).getbbox()
    if bbox:
        left, top, right, bottom = bbox
        pad = max(2, int(max(right - left, bottom - top) * 0.01))
        left = max(0, left - pad)
        top = max(0, top - pad)
        right = min(im.width, right + pad)
        bottom = min(im.height, bottom + pad)
        side = max(right - left, bottom - top)
        cx = (left + right) // 2
        cy = (top + bottom) // 2
        left = max(0, min(im.width - side, cx - side // 2))
        top = max(0, min(im.height - side, cy - side // 2))
        im = im.crop((left, top, left + side, top + side))
    else:
        w, h = im.size
        side = min(w, h)
        left = (w - side) // 2
        top = (h - side) // 2
        im = im.crop((left, top, left + side, top + side))
    im = im.resize((size, size), Image.Resampling.LANCZOS)
    im = ImageEnhance.Contrast(im).enhance(1.06)
    im = ImageEnhance.Brightness(im).enhance(0.97)
    return im.convert("L")


def mask_to_disk(gray_im: Image.Image) -> Image.Image:
    size = gray_im.width
    out = Image.new("L", (size, size), 0)
    src = gray_im.load()
    dst = out.load()
    cx = cy = (size - 1) / 2.0
    r = size / 2.0 - 0.5
    r2 = r * r
    for y in range(size):
        for x in range(size):
            dx = x - cx
            dy = y - cy
            if dx * dx + dy * dy <= r2:
                dst[x, y] = int(src[x, y])
    return out


def build_texture_image(im: Image.Image, size: int) -> Image.Image:
    """Return a full-frame 8-bit greyscale moon image."""
    if im.width >= im.height * 1.75:
        return mask_to_disk(render_equirect_nearside(im, size))
    return mask_to_disk(crop_photo_disk(im, size))


def image_to_gray_list(gray_im: Image.Image) -> list[int]:
    gray: list[int] = []
    for y in range(gray_im.height):
        for x in range(gray_im.width):
            gray.append(int(gray_im.getpixel((x, y))))
    return gray


def write_png(path: Path, gray_im: Image.Image) -> None:
    size = gray_im.width
    rgba = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    src = gray_im.load()
    dst = rgba.load()
    cx = cy = (size - 1) / 2.0
    r = size / 2.0 - 0.5
    r2 = r * r
    for y in range(size):
        for x in range(size):
            dx = x - cx
            dy = y - cy
            if dx * dx + dy * dy <= r2:
                v = int(src[x, y])
                dst[x, y] = (v, v, v, 255)
    path.parent.mkdir(parents=True, exist_ok=True)
    rgba.save(path)
    print(f"Wrote {path} ({size}x{size} RGBA PNG)")


def write_header(path: Path, size: int, gray: list[int]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    n = size * size
    lines = [
        "// Auto-generated by scripts/embed_moon_texture.py — do not edit.",
        "// Source: NASA SVS CGI Moon Kit / LRO color map. 8-bit grey; disk mask in pm_moon_draw.",
        "#pragma once",
        "#include <stdint.h>",
        f"#define MOON_TEX_SIZE {size}",
        f"#define MOON_TEX_PIXELS {n}",
        "",
        f"static const uint8_t kMoonTextureGray[MOON_TEX_PIXELS] PROGMEM = {{",
    ]
    buf = "  "
    for i, v in enumerate(gray):
        buf += f"{v},"
        if (i + 1) % 24 == 0:
            lines.append(buf)
            buf = "  "
    if buf.strip():
        lines.append(buf.rstrip(","))
    lines.append("};")
    lines.append("")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"Wrote {path} ({n} pixels, {n // 1024} KiB grey PROGMEM)")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--size", type=int, default=320, help="texture side in pixels (default 320)")
    ap.add_argument("--input", type=Path, default=None)
    ap.add_argument("--url", default=DEFAULT_URL)
    ap.add_argument("-o", type=Path, default=DEFAULT_OUT)
    ap.add_argument("--png-out", type=Path, default=None)
    args = ap.parse_args()
    im = load_source(args.input, args.url)
    gray_im = build_texture_image(im, args.size)
    gray = image_to_gray_list(gray_im)
    write_header(args.o, args.size, gray)
    if args.png_out:
        write_png(args.png_out, gray_im)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
