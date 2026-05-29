"""Shared round-viewport logic for viewer and CI (no display required)."""

from __future__ import annotations

import struct
from pathlib import Path

SIZE = 466
SCALE = 2
WIN = SIZE * SCALE
CENTER = WIN // 2
RADIUS = int(218 * SCALE)


def touch_from_window(x: int, y: int, scale: int = SCALE) -> tuple[int, int]:
  """Map window coordinates to 466×466 panel coordinates."""
  return int(x / scale), int(y / scale)


def load_bmp24(path: Path) -> tuple[bytes, int, int] | None:
  """
  Load 24-bit BMP (watch screen.bmp format). Returns (BGR pixel bytes, w, h).
  """
  data = path.read_bytes()
  if len(data) < 54 or data[:2] != b"BM":
    return None
  offset = struct.unpack_from("<I", data, 10)[0]
  w, h = struct.unpack_from("<ii", data, 18)
  if w <= 0 or h <= 0:
    return None
  row_bytes = w * 3
  pixels = data[offset : offset + row_bytes * h]
  if len(pixels) < row_bytes * h:
    return None
  return pixels, w, h


def validate_watch_bmp(path: Path) -> bool:
  loaded = load_bmp24(path)
  if loaded is None:
    return False
  _, w, h = loaded
  return w == SIZE and h == SIZE


def make_test_bmp(path: Path, width: int = SIZE, height: int = SIZE) -> None:
  """Write a minimal 24-bit BMP for CI fixtures."""
  row = width * 3
  pad = (4 - (row % 4)) % 4
  row_padded = row + pad
  pixel_bytes = row_padded * height
  file_size = 54 + pixel_bytes
  header = bytearray(54)
  header[0:2] = b"BM"
  struct.pack_into("<I", header, 2, file_size)
  struct.pack_into("<I", header, 10, 54)
  struct.pack_into("<i", header, 18, width)
  struct.pack_into("<i", header, 22, height)
  struct.pack_into("<H", header, 28, 1)
  struct.pack_into("<H", header, 30, 24)
  pixels = bytearray(pixel_bytes)
  for y in range(height):
    for x in range(width):
      i = y * row_padded + x * 3
      pixels[i] = (x * 7) % 256
      pixels[i + 1] = (y * 5) % 256
      pixels[i + 2] = 40
  path.write_bytes(bytes(header) + bytes(pixels))
