#!/usr/bin/env python3
"""Generate the compact Google Noto Color Emoji pack used by the Codex dial."""

from __future__ import annotations

from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parents[1]
FONT = Path.home() / "Library" / "Fonts" / "NotoColorEmoji.ttf"
OUT_BIN = ROOT / "astrolabe175c" / "main" / "assets" / "codex_noto_emoji_40_argb.bin"
OUT_H = ROOT / "astrolabe175c" / "main" / "faculty175_codex_emoji.h"
SIZE = 40
FONT_SIZE = 109

CATALOG = [
    ("COMPUTER", "💻"),
    ("TOOLS", "🛠️"),
    ("BUG", "🐛"),
    ("ROCKET", "🚀"),
    ("EXPERIMENT", "🔬"),
    ("WRITING", "📝"),
    ("SEARCH", "🔍"),
    ("DESIGN", "🎨"),
    ("SECURITY", "🔒"),
    ("WEB", "🌐"),
    ("DATABASE", "🗄️"),
    ("ANALYTICS", "📊"),
    ("ROBOT", "🤖"),
    ("THINKING", "🧠"),
    ("MAGIC", "✨"),
    ("URGENT", "🔥"),
    ("PACKAGE", "📦"),
    ("MOBILE", "📱"),
    ("CAMERA", "📷"),
    ("AUDIO", "🎙️"),
    ("TIME", "⏱️"),
    ("SUCCESS", "✅"),
    ("WARNING", "⚠️"),
    ("WORLD", "🌍"),
    ("BOOKS", "📚"),
    ("IDEA", "💡"),
    ("MESSAGE", "💬"),
    ("FOLDER", "📁"),
    ("CLOUD", "☁️"),
    ("LINK", "🔗"),
    ("HEART", "❤️"),
    ("STAR", "⭐"),
]


def render(font: ImageFont.FreeTypeFont, text: str) -> Image.Image:
    canvas = Image.new("RGBA", (192, 192), (0, 0, 0, 0))
    draw = ImageDraw.Draw(canvas)
    bbox = draw.textbbox((0, 0), text, font=font, embedded_color=True)
    x = (canvas.width - (bbox[2] - bbox[0])) // 2 - bbox[0]
    y = (canvas.height - (bbox[3] - bbox[1])) // 2 - bbox[1]
    draw.text((x, y), text, font=font, embedded_color=True)
    visible = canvas.getbbox()
    if visible is None:
        raise RuntimeError(f"Noto rendered an empty glyph for {text!r}")
    glyph = canvas.crop(visible)
    glyph.thumbnail((SIZE - 2, SIZE - 2), Image.Resampling.LANCZOS)
    out = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    out.alpha_composite(glyph, ((SIZE - glyph.width) // 2, (SIZE - glyph.height) // 2))
    return out


def bgra_bytes(image: Image.Image) -> bytes:
    data = bytearray()
    for r, g, b, a in image.getdata():
        data.extend((b, g, r, a))
    return bytes(data)


def main() -> None:
    if not FONT.exists():
        raise SystemExit(f"missing Google Noto Color Emoji font: {FONT}")
    font = ImageFont.truetype(str(FONT), FONT_SIZE)
    glyphs = [render(font, emoji) for _name, emoji in CATALOG]
    glyph_bytes = SIZE * SIZE * 4
    OUT_BIN.parent.mkdir(parents=True, exist_ok=True)
    with OUT_BIN.open("wb") as output:
        output.write(b"CODEXEM1")
        output.write(SIZE.to_bytes(2, "little"))
        output.write(SIZE.to_bytes(2, "little"))
        output.write((SIZE * 4).to_bytes(2, "little"))
        output.write(len(CATALOG).to_bytes(2, "little"))
        output.write(glyph_bytes.to_bytes(4, "little"))
        for glyph in glyphs:
            output.write(bgra_bytes(glyph))

    names = "\n".join(f"    FACULTY175_CODEX_EMOJI_{name} = {i}," for i, (name, _emoji) in enumerate(CATALOG))
    OUT_H.write_text(
        f"""#pragma once

#define FACULTY175_CODEX_EMOJI_COUNT {len(CATALOG)}
#define FACULTY175_CODEX_EMOJI_W {SIZE}
#define FACULTY175_CODEX_EMOJI_H {SIZE}
#define FACULTY175_CODEX_EMOJI_ROW_BYTES {SIZE * 4}
#define FACULTY175_CODEX_EMOJI_BYTES {glyph_bytes}

typedef enum {{
{names}
}} faculty175_codex_emoji_t;
""",
        encoding="utf-8",
    )
    print(f"generated {len(CATALOG)} Google Noto Color Emoji sprites at {OUT_BIN}")


if __name__ == "__main__":
    main()
