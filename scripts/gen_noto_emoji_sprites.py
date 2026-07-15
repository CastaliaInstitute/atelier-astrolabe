#!/usr/bin/env python3
"""Generate small RGB565+alpha Noto Color Emoji sprites for Elecrow128."""

from __future__ import annotations

from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


FONT = "/usr/share/fonts/truetype/noto/NotoColorEmoji.ttf"
OUT = Path("elecrow128/main/noto_emoji_sprites.h")
SMALL_SIZE = 24
LARGE_SIZE = 54
STRIKE = 109
EMOJI = [
    ("EASE", "\U0001f642"),
    ("JOY", "\U0001f604"),
    ("SPARK", "\U0001f929"),
    ("ALERT", "\U0001f62e"),
    ("TENSE", "\U0001f630"),
    ("ANGER", "\U0001f621"),
    ("SAD", "\u2639\ufe0f"),
    ("LOW", "\U0001f61e"),
    ("SLEEP", "\U0001f634"),
    ("CALM", "\U0001f60c"),
    ("COOL", "\U0001f60e"),
    ("BRIGHT", "\U0001f603"),
]


def rgb565(r: int, g: int, b: int) -> int:
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def render_emoji(font: ImageFont.FreeTypeFont, emoji: str, size: int) -> Image.Image:
    canvas = Image.new("RGBA", (180, 180), (0, 0, 0, 0))
    draw = ImageDraw.Draw(canvas)
    draw.text((36, 24), emoji, font=font, embedded_color=True)
    bbox = canvas.getbbox()
    if bbox is None:
        raise RuntimeError(f"empty emoji render: {emoji!r}")
    glyph = canvas.crop(bbox)
    side = max(glyph.size)
    square = Image.new("RGBA", (side, side), (0, 0, 0, 0))
    square.alpha_composite(glyph, ((side - glyph.width) // 2, (side - glyph.height) // 2))
    return square.resize((size, size), Image.Resampling.LANCZOS)


def emit_rgb565_array(lines: list[str], name: str, sprites: list[Image.Image], size: int) -> None:
    lines.append(f"static const uint16_t {name}[NOTO_EMOJI_SPRITE_COUNT][{size} * {size}] = {{")
    for sprite in sprites:
        values = []
        for r, g, b, _a in sprite.getdata():
            values.append(f"0x{rgb565(r, g, b):04x}")
        lines.append("    {" + ", ".join(values) + "},")
    lines.append("};")
    lines.append("")


def emit_alpha_array(lines: list[str], name: str, sprites: list[Image.Image], size: int) -> None:
    lines.append(f"static const uint8_t {name}[NOTO_EMOJI_SPRITE_COUNT][{size} * {size}] = {{")
    for sprite in sprites:
        values = [str(a) for *_rgb, a in sprite.getdata()]
        lines.append("    {" + ", ".join(values) + "},")
    lines.append("};")
    lines.append("")


def main() -> None:
    font = ImageFont.truetype(FONT, STRIKE)
    small_sprites = [render_emoji(font, emoji, SMALL_SIZE) for _, emoji in EMOJI]
    large_sprites = [render_emoji(font, emoji, LARGE_SIZE) for _, emoji in EMOJI]

    lines = [
        "#pragma once",
        "",
        "#include <stdint.h>",
        "",
        f"#define NOTO_EMOJI_SPRITE_COUNT {len(small_sprites)}",
        f"#define NOTO_EMOJI_SMALL_W {SMALL_SIZE}",
        f"#define NOTO_EMOJI_SMALL_H {SMALL_SIZE}",
        f"#define NOTO_EMOJI_LARGE_W {LARGE_SIZE}",
        f"#define NOTO_EMOJI_LARGE_H {LARGE_SIZE}",
        "",
        "static const char *const s_noto_emoji_names[NOTO_EMOJI_SPRITE_COUNT] = {",
    ]
    for name, _emoji in EMOJI:
        lines.append(f'    "{name}",')
    lines.append("};")
    lines.append("")
    emit_rgb565_array(lines, "s_noto_emoji_small_rgb565", small_sprites, SMALL_SIZE)
    emit_alpha_array(lines, "s_noto_emoji_small_alpha", small_sprites, SMALL_SIZE)
    emit_rgb565_array(lines, "s_noto_emoji_large_rgb565", large_sprites, LARGE_SIZE)
    emit_alpha_array(lines, "s_noto_emoji_large_alpha", large_sprites, LARGE_SIZE)

    OUT.write_text("\n".join(lines), encoding="utf-8")
    print(OUT)


if __name__ == "__main__":
    main()
