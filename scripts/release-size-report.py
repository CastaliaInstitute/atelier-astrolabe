#!/usr/bin/env python3
"""Report per-release firmware size against active and planned OTA slots."""
from __future__ import annotations

import argparse
import csv
import os
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "config" / "release_variants.csv"
DEFAULT_BUILD_DIR = Path(os.environ.get("PLATFORMIO_BUILD_DIR", "/tmp/astrolabe-pio-build"))


def parse_size(value: str) -> int:
    value = value.strip().lower()
    if value.startswith("0x"):
        return int(value, 16)
    if value.endswith("k"):
        return int(value[:-1]) * 1024
    if value.endswith("m"):
        return int(value[:-1]) * 1024 * 1024
    return int(value)


def fmt_size(value: int | None) -> str:
    if value is None:
        return "-"
    return f"{value / 1024:.1f} KB"


def gen_esp32part() -> Path | None:
    candidates = [
        Path.home() / ".platformio/packages/framework-arduinoespressif32/tools/gen_esp32part.py",
        Path.home() / ".platformio/packages/framework-espidf/components/partition_table/gen_esp32part.py",
    ]
    for path in candidates:
        if path.exists():
            return path
    for path in (Path.home() / ".platformio").glob("**/gen_esp32part.py"):
        return path
    return None


def decode_partitions(partitions_bin: Path) -> list[dict[str, str]]:
    decoder = gen_esp32part()
    if decoder is None or not partitions_bin.exists():
        return []
    proc = subprocess.run(
        [sys.executable, str(decoder), str(partitions_bin)],
        cwd=ROOT,
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
    )
    rows: list[dict[str, str]] = []
    for line in proc.stdout.splitlines():
        line = line.strip()
        if not line or line.startswith("#") or "," not in line:
            continue
        parts = [part.strip() for part in line.split(",")]
        if len(parts) < 5:
            continue
        rows.append(
            {
                "name": parts[0],
                "type": parts[1],
                "subtype": parts[2],
                "offset": parts[3],
                "size": parts[4],
            }
        )
    return rows


def active_ota_slot(build_root: Path, env: str) -> tuple[int | None, int]:
    partitions = decode_partitions(build_root / env / "partitions.bin")
    app_slots = [parse_size(row["size"]) for row in partitions if row["type"] == "app"]
    ota_slots = [
        parse_size(row["size"])
        for row in partitions
        if row["type"] == "app" and row["subtype"].startswith("ota_")
    ]
    if len(ota_slots) >= 2:
        return min(ota_slots), len(ota_slots)
    if app_slots:
        return max(app_slots), len(ota_slots)
    return None, len(ota_slots)


def build_env(env: str, build_root: Path) -> None:
    run_env = os.environ.copy()
    run_env["PIO_ENV"] = env
    run_env["PLATFORMIO_BUILD_DIR"] = str(build_root)
    subprocess.run([str(ROOT / "scripts" / "build.sh")], cwd=ROOT, env=run_env, check=True)


def load_manifest(path: Path) -> list[dict[str, str]]:
    with path.open(newline="") as f:
        return list(csv.DictReader(f))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, default=MANIFEST)
    parser.add_argument("--build-dir", type=Path, default=DEFAULT_BUILD_DIR)
    parser.add_argument("--build", action="store_true", help="build each env before reporting")
    parser.add_argument("--env", action="append", dest="envs", help="limit report to one or more PIO envs")
    args = parser.parse_args()

    rows = load_manifest(args.manifest)
    if args.envs:
        wanted = set(args.envs)
        rows = [row for row in rows if row["pio_env"] in wanted]

    failures = 0
    print("| Release | Env | Device flash | Firmware | Active OTA | Planned OTA | Status |")
    print("|---|---|---:|---:|---:|---:|---|")
    for row in rows:
        env = row["pio_env"]
        if args.build:
            build_env(env, args.build_dir)
        firmware = args.build_dir / env / "firmware.bin"
        if not firmware.exists():
            alt = ROOT / ".pio" / "build" / env / "firmware.bin"
            firmware = alt if alt.exists() else firmware
        if not firmware.exists():
            failures += 1
            print(f"| {row['product_name']} | `{env}` | {fmt_size(parse_size(row['flash_bytes']))} | - | - | - | missing build |")
            continue

        build_root = firmware.parents[1]
        size = firmware.stat().st_size
        flash_size = parse_size(row["flash_bytes"])
        active_slot, ota_count = active_ota_slot(build_root, env)
        planned_slot = parse_size(row["planned_ota_slot_bytes"])
        active_ok = active_slot is not None and ota_count >= 2 and size <= active_slot
        planned_ok = size <= planned_slot
        status = []
        status.append("active OTA fits" if active_ok else "active OTA blocked")
        status.append("planned slot fits" if planned_ok else "planned slot too small")
        if not active_ok or not planned_ok:
            failures += 1
        print(
            f"| {row['product_name']} | `{env}` | {fmt_size(flash_size)} | {fmt_size(size)} | "
            f"{fmt_size(active_slot)} | {fmt_size(planned_slot)} | {', '.join(status)} |"
        )

    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
