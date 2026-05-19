#!/usr/bin/env python3
"""Hardware: Castalia (face 8) → qa inject swipe left → assert Synastry (face 9).

Thin wrapper around functional_test.py (matrix: tests/functional/faces_astrolabe.json).

  ./scripts/test_castalia_swipe.py
  ./scripts/test_castalia_swipe.py --flash
  ./scripts/test_castalia_swipe.py --port /dev/cu.usbmodem1101
"""
from __future__ import annotations

import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
FT = REPO / "scripts" / "functional_test.py"


def main() -> int:
    cmd = [sys.executable, str(FT), "--faces", "castalia", *sys.argv[1:]]
    return subprocess.call(cmd, cwd=str(REPO))


if __name__ == "__main__":
    sys.exit(main())
