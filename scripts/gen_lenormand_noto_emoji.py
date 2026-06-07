#!/usr/bin/env python3
"""Generate a compact Noto Color Emoji glyph pack for oracle faces."""

from __future__ import annotations

import argparse
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_FONT = Path.home() / "Library" / "Fonts" / "NotoColorEmoji.ttf"
OUT_H = ROOT / "faculty175" / "main" / "faculty175_lenormand_glyphs.h"
OUT_BIN = ROOT / "faculty175" / "storage_seed" / "lenormand" / "noto_color_emoji_48_argb.bin"

CARDS = [
    ("RIDER", "\U0001f3c7"),
    ("CLOVER", "\U0001f340"),
    ("SHIP", "\U0001f6a2"),
    ("HOUSE", "\U0001f3e0"),
    ("TREE", "\U0001f333"),
    ("CLOUDS", "\u2601\ufe0f"),
    ("SNAKE", "\U0001f40d"),
    ("COFFIN", "\u26b0\ufe0f"),
    ("BOUQUET", "\U0001f490"),
    ("SCYTHE", "\U0001fa93"),
    ("WHIP", "\U0001f9f9"),
    ("BIRDS", "\U0001f426"),
    ("CHILD", "\U0001f9d2"),
    ("FOX", "\U0001f98a"),
    ("BEAR", "\U0001f43b"),
    ("STARS", "\u2728"),
    ("STORK", "\U0001fabf"),
    ("DOG", "\U0001f415"),
    ("TOWER", "\U0001f5fc"),
    ("GARDEN", "\U0001f3de\ufe0f"),
    ("MOUNTAIN", "\u26f0\ufe0f"),
    ("CROSSROADS", "\U0001f6e4\ufe0f"),
    ("MICE", "\U0001f401"),
    ("HEART", "\u2764\ufe0f"),
    ("RING", "\U0001f48d"),
    ("BOOK", "\U0001f4d6"),
    ("LETTER", "\u2709\ufe0f"),
    ("MAN", "\U0001f468"),
    ("WOMAN", "\U0001f469"),
    ("LILY", "\u269c\ufe0f"),
    ("SUN", "\u2600\ufe0f"),
    ("MOON", "\U0001f319"),
    ("KEY", "\U0001f511"),
    ("FISH", "\U0001f41f"),
    ("ANCHOR", "\u2693"),
    ("CROSS", "\u271d\ufe0f"),
]


def render_color(font: ImageFont.FreeTypeFont, text: str, size: int) -> Image.Image:
    canvas_size = 192
    canvas = Image.new("RGBA", (canvas_size, canvas_size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(canvas)
    bbox = draw.textbbox((0, 0), text, font=font, embedded_color=True)
    w = bbox[2] - bbox[0]
    h = bbox[3] - bbox[1]
    x = (canvas.width - w) // 2 - bbox[0]
    y = (canvas.height - h) // 2 - bbox[1]
    draw.text((x, y), text, font=font, embedded_color=True)

    bbox = canvas.getbbox()
    if bbox is None:
        return Image.new("RGBA", (size, size), (0, 0, 0, 0))
    glyph = canvas.crop(bbox)
    glyph.thumbnail((size - 6, size - 6), Image.Resampling.LANCZOS)
    out = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    out.paste(glyph, ((size - glyph.width) // 2, (size - glyph.height) // 2), glyph)
    return out


def bgra_bytes(img: Image.Image) -> bytes:
    rgba = img.convert("RGBA")
    data = bytearray()
    for r, g, b, a in rgba.getdata():
        data.extend((b, g, r, a))
    return bytes(data)


def contact_sheet(images: list[Image.Image], out_path: Path) -> None:
    cell = 112
    sheet = Image.new("RGBA", (6 * cell, 6 * cell), (10, 9, 16, 255))
    for i, img in enumerate(images):
        x = (i % 6) * cell + (cell - img.width) // 2
        y = (i // 6) * cell + (cell - img.height) // 2
        sheet.alpha_composite(img, (x, y))
    out_path.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(out_path)


def write_files(font_path: Path, size: int, font_size: int) -> None:
    font = ImageFont.truetype(str(font_path), font_size)
    glyphs = [render_color(font, text, size) for _, text in CARDS]
    glyph_bytes = size * size * 4

    OUT_BIN.parent.mkdir(parents=True, exist_ok=True)
    with OUT_BIN.open("wb") as f:
        f.write(b"LENOCLR1")
        f.write(size.to_bytes(2, "little"))
        f.write(size.to_bytes(2, "little"))
        f.write((size * 4).to_bytes(2, "little"))
        f.write(len(CARDS).to_bytes(2, "little"))
        f.write(glyph_bytes.to_bytes(4, "little"))
        for glyph in glyphs:
            f.write(bgra_bytes(glyph))

    OUT_H.write_text(
        "\n".join(
            [
                "#pragma once",
                "",
                "#include <stdint.h>",
                "",
                f"#define FACULTY175_LENORMAND_GLYPH_COUNT {len(CARDS)}",
                f"#define FACULTY175_LENORMAND_GLYPH_W {size}",
                f"#define FACULTY175_LENORMAND_GLYPH_H {size}",
                f"#define FACULTY175_LENORMAND_GLYPH_ROW_BYTES {size * 4}",
                f"#define FACULTY175_LENORMAND_GLYPH_BYTES {glyph_bytes}",
                f'#define FACULTY175_LENORMAND_GLYPH_PATH "/bust_cache/lenormand/noto_color_emoji_{size}_argb.bin"',
                "",
            ]
        )
    )
    contact_sheet(glyphs, ROOT / "artifacts" / "qa" / "lenormand-noto-color-emoji-contact.png")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--font", type=Path, default=DEFAULT_FONT)
    ap.add_argument("--size", type=int, default=48)
    ap.add_argument("--font-size", type=int, default=109)
    args = ap.parse_args()
    if not args.font.exists():
        raise SystemExit(f"missing Noto Color Emoji font: {args.font}")
    write_files(args.font, args.size, args.font_size)
    print(f"generated {OUT_BIN} from {args.font}")


if __name__ == "__main__":
    main()
