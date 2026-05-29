#!/usr/bin/env python3
"""
Level 1 host simulator — 466×466 round viewport with mouse-as-touch.

Usage:
  python3 sim/host_round/viewer.py
  python3 sim/host_round/viewer.py artifacts/qa-spotify-2026-05-17.bmp

Requires: pygame (`pip install pygame`)
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from sim_core import CENTER, RADIUS, SCALE, SIZE, WIN, load_bmp24, validate_watch_bmp

try:
    import pygame
except ImportError:
    print("Install pygame: pip install pygame", file=sys.stderr)
    raise SystemExit(1)


def load_bmp565(path: Path) -> pygame.Surface | None:
    """Load a 24-bit BMP from the watch HTTP server into an RGB surface."""
    if not validate_watch_bmp(path):
        return None
    pixels, w, h = load_bmp24(path)  # type: ignore[misc]
    surf = pygame.image.frombuffer(pixels, (w, h), "BGR")
    return surf.convert()


def apply_round_mask(surf: pygame.Surface) -> pygame.Surface:
    masked = surf.copy()
    mask = pygame.Surface((WIN, WIN), pygame.SRCALPHA)
    pygame.draw.circle(mask, (255, 255, 255, 255), (CENTER, CENTER), RADIUS)
    masked = pygame.transform.scale(masked, (WIN, WIN))
    masked.blit(mask, (0, 0), special_flags=pygame.BLEND_RGBA_MULT)
    return masked


def main() -> None:
    parser = argparse.ArgumentParser(description="Mynah round host viewer")
    parser.add_argument("bmp", nargs="?", type=Path, help="Optional screen.bmp capture")
    args = parser.parse_args()

    pygame.init()
    screen = pygame.display.set_mode((WIN, WIN))
    pygame.display.set_caption("Mynah host sim — 466×466")

    if args.bmp and args.bmp.is_file():
        frame = load_bmp565(args.bmp)
        if frame is None:
            raise SystemExit(f"Could not load {args.bmp}")
    else:
        frame = pygame.Surface((SIZE, SIZE))
        frame.fill((12, 12, 18))

    clock = pygame.time.Clock()
    touch_down = False
    touch_pos = (CENTER, CENTER)

    font = pygame.font.SysFont("sans", 18)

    while True:
        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                pygame.quit()
                return
            if event.type == pygame.MOUSEBUTTONDOWN:
                touch_down = True
                touch_pos = event.pos
            if event.type == pygame.MOUSEBUTTONUP:
                touch_down = False
            if event.type == pygame.MOUSEMOTION and touch_down:
                touch_pos = event.pos

        screen.fill((0, 0, 0))
        scaled = apply_round_mask(frame)
        screen.blit(scaled, (0, 0))

        if touch_down:
            pygame.draw.circle(screen, (80, 200, 255), touch_pos, 8, 2)

        tx = int(touch_pos[0] / SCALE)
        ty = int(touch_pos[1] / SCALE)
        label = font.render(
            f"touch ({tx},{ty}) {'DOWN' if touch_down else 'up'}", True, (180, 180, 200)
        )
        screen.blit(label, (8, WIN - 28))

        pygame.display.flip()
        clock.tick(60)


if __name__ == "__main__":
    main()
