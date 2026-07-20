#!/usr/bin/env python3
"""Verify a LunaSay power report and every source file it consumed."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import sys


EXPECTED_OUTPUTS = {
    "report.md",
    "runs.csv",
    "deep_sleep_runs.csv",
    "curves.csv",
    "curves.svg",
    "analyzer_sources.json",
    "artifact_sources.json",
    "report_config.json",
}


def load_json(path: Path):
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise ValueError(f"cannot read {path}: {exc}") from exc


def digest(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            value.update(chunk)
    return value.hexdigest()


def verify_file(record: dict, label: str, expected_path: Path | None = None) -> None:
    if not isinstance(record, dict):
        raise ValueError(f"{label}: manifest record must be an object")
    raw_path = record.get("path")
    expected_hash = record.get("sha256")
    expected_bytes = record.get("bytes")
    if not isinstance(raw_path, str) or not raw_path:
        raise ValueError(f"{label}: missing path")
    if (
        not isinstance(expected_hash, str)
        or len(expected_hash) != 64
        or any(char not in "0123456789abcdef" for char in expected_hash)
    ):
        raise ValueError(f"{label}: invalid SHA-256")
    if expected_bytes is not None and (
        not isinstance(expected_bytes, int) or isinstance(expected_bytes, bool) or expected_bytes < 0
    ):
        raise ValueError(f"{label}: invalid byte count")
    path = Path(raw_path)
    if expected_path is not None and path.resolve() != expected_path.resolve():
        raise ValueError(f"{label}: path substitution: expected {expected_path}, got {path}")
    if not path.is_file():
        raise ValueError(f"{label}: missing file: {path}")
    if expected_bytes is not None and path.stat().st_size != expected_bytes:
        raise ValueError(
            f"{label}: size mismatch for {path}: expected {expected_bytes}, got {path.stat().st_size}"
        )
    actual_hash = digest(path)
    if actual_hash != expected_hash:
        raise ValueError(
            f"{label}: SHA-256 mismatch for {path}: expected {expected_hash}, got {actual_hash}"
        )


def verify_source_manifest(path: Path, label: str) -> int:
    records = load_json(path)
    if not isinstance(records, list):
        raise ValueError(f"{label}: manifest must be an array")
    seen: set[Path] = set()
    for index, record in enumerate(records):
        verify_file(record, f"{label}[{index}]")
        resolved = Path(record["path"]).resolve()
        if resolved in seen:
            raise ValueError(f"{label}: duplicate source path: {resolved}")
        seen.add(resolved)
    return len(records)


def verify_config(path: Path) -> None:
    config = load_json(path)
    if not isinstance(config, dict):
        raise ValueError("report_config.json: configuration must be an object")
    for name in ("matrix", "generator"):
        raw_path = config.get(f"{name}_path" if name == "generator" else name)
        expected_hash = config.get(f"{name}_sha256")
        if not isinstance(raw_path, str) or not raw_path:
            raise ValueError(f"report_config.json: missing {name} path")
        verify_file(
            {"path": raw_path, "sha256": expected_hash},
            f"report_config.json:{name}",
        )


def verify_outputs(report_dir: Path) -> int:
    manifest_path = report_dir / "report_outputs.json"
    manifest = load_json(manifest_path)
    if not isinstance(manifest, dict) or manifest.get("schema_version") != 1:
        raise ValueError("report_outputs.json: unsupported or missing schema_version")
    records = manifest.get("files")
    if not isinstance(records, list):
        raise ValueError("report_outputs.json: files must be an array")
    by_name: dict[str, dict] = {}
    for record in records:
        if not isinstance(record, dict) or not isinstance(record.get("path"), str):
            raise ValueError("report_outputs.json: invalid file record")
        name = Path(record["path"]).name
        if name in by_name:
            raise ValueError(f"report_outputs.json: duplicate output: {name}")
        by_name[name] = record
    actual_names = set(by_name)
    if actual_names != EXPECTED_OUTPUTS:
        raise ValueError(
            "report_outputs.json: output set mismatch: "
            f"missing={sorted(EXPECTED_OUTPUTS - actual_names)}, "
            f"unexpected={sorted(actual_names - EXPECTED_OUTPUTS)}"
        )
    for name in sorted(EXPECTED_OUTPUTS):
        verify_file(by_name[name], f"report_outputs.json:{name}", report_dir / name)
    return len(records)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--report-dir",
        type=Path,
        default=Path("artifacts/qa/lunasay-power-report"),
    )
    args = parser.parse_args()
    report_dir = args.report_dir.resolve()
    try:
        output_count = verify_outputs(report_dir)
        artifact_count = verify_source_manifest(
            report_dir / "artifact_sources.json", "artifact_sources.json"
        )
        analyzer_count = verify_source_manifest(
            report_dir / "analyzer_sources.json", "analyzer_sources.json"
        )
        verify_config(report_dir / "report_config.json")
    except ValueError as exc:
        print(f"lunasay_power_report_verify: FAIL: {exc}", file=sys.stderr)
        return 1
    print(
        "lunasay_power_report_verify: PASS: "
        f"{output_count} outputs, {artifact_count} QA sources, "
        f"{analyzer_count} analyzer sources"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
