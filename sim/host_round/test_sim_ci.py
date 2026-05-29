#!/usr/bin/env python3
"""Headless simulator checks for CI (unittest)."""

from __future__ import annotations

import os
import tempfile
import unittest
from pathlib import Path

from sim_core import (
    CENTER,
    RADIUS,
    SIZE,
    WIN,
    load_bmp24,
    make_test_bmp,
    touch_from_window,
    validate_watch_bmp,
)


class TestSimCore(unittest.TestCase):
  def test_board_constants(self) -> None:
    self.assertEqual(SIZE, 466)
    self.assertEqual(WIN, SIZE * 2)
    self.assertGreater(RADIUS, 200)
    self.assertEqual(CENTER, WIN // 2)

  def test_touch_mapping(self) -> None:
    self.assertEqual(touch_from_window(200, 100), (100, 50))
    self.assertEqual(touch_from_window(0, 0), (0, 0))

  def test_bmp_roundtrip(self) -> None:
    with tempfile.TemporaryDirectory() as tmp:
      path = Path(tmp) / "frame.bmp"
      make_test_bmp(path)
      self.assertTrue(validate_watch_bmp(path))
      loaded = load_bmp24(path)
      self.assertIsNotNone(loaded)
      pixels, w, h = loaded  # type: ignore[misc]
      self.assertEqual((w, h), (SIZE, SIZE))
      self.assertEqual(len(pixels), w * h * 3)

  def test_reject_wrong_size(self) -> None:
    with tempfile.TemporaryDirectory() as tmp:
      path = Path(tmp) / "small.bmp"
      make_test_bmp(path, width=100, height=100)
      self.assertFalse(validate_watch_bmp(path))


class TestPygameHeadless(unittest.TestCase):
  """Smoke-test pygame with dummy video driver (GitHub Actions)."""

  def test_pygame_dummy_one_frame(self) -> None:
    os.environ.setdefault("SDL_VIDEODRIVER", "dummy")
    os.environ.setdefault("PYGAME_HIDE_SUPPORT_PROMPT", "1")
    import pygame

    pygame.init()
    try:
      surf = pygame.Surface((SIZE, SIZE))
      surf.fill((12, 12, 18))
      screen = pygame.display.set_mode((WIN, WIN))
      scaled = pygame.transform.scale(surf, (WIN, WIN))
      screen.blit(scaled, (0, 0))
      pygame.display.flip()
      pygame.event.pump()
    finally:
      pygame.quit()


if __name__ == "__main__":
  unittest.main()
