#!/usr/bin/env python3
"""Copy built release firmware into docs/releases/integration for Pages."""
from __future__ import annotations

import csv
import argparse
import hashlib
import json
import os
import shutil
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "config" / "release_variants.csv"
DEFAULT_BUILD_DIR = Path(os.environ.get("ASTROLABE_RELEASE_ARTIFACT_DIR", os.environ.get("PLATFORMIO_BUILD_DIR", "/tmp/astrolabe-pio-build")))
OUT = ROOT / "docs" / "releases" / "integration"


def git_value(*args: str) -> str:
    return subprocess.check_output(["git", *args], cwd=ROOT, text=True).strip()


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=DEFAULT_BUILD_DIR)
    args = parser.parse_args()

    with MANIFEST.open(newline="") as f:
        rows = list(csv.DictReader(f))

    OUT.mkdir(parents=True, exist_ok=True)
    git_sha = git_value("rev-parse", "HEAD")
    git_ref = git_value("rev-parse", "--abbrev-ref", "HEAD")
    published = []

    for row in rows:
        env = row["pio_env"]
        src = args.build_dir / env / "firmware.bin"
        if not src.exists():
            src = ROOT / ".pio" / "build" / env / "firmware.bin"
        if not src.exists():
            raise FileNotFoundError(f"missing firmware for {env}: checked {args.build_dir / env / 'firmware.bin'} and {src}")

        channel_dir = OUT / row["ota_channel"]
        channel_dir.mkdir(parents=True, exist_ok=True)
        dst = channel_dir / "firmware.bin"
        shutil.copy2(src, dst)
        meta = {
            "release_id": row["release_id"],
            "product_name": row["product_name"],
            "device_platform": row["device_platform"],
            "firmware_variant": row["firmware_variant"],
            "pio_env": row["pio_env"],
            "ota_channel": row["ota_channel"],
            "git_ref": git_ref,
            "git_sha": git_sha,
            "firmware_bytes": dst.stat().st_size,
            "sha256": sha256(dst),
            "firmware_url": f"releases/integration/{row['ota_channel']}/firmware.bin",
        }
        (channel_dir / "manifest.json").write_text(json.dumps(meta, indent=2) + "\n")
        published.append(meta)

    (OUT / "manifest.json").write_text(json.dumps({"git_ref": git_ref, "git_sha": git_sha, "releases": published}, indent=2) + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
