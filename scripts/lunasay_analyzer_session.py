"""Lifecycle and validation for an external LunaSay battery analyzer adapter."""

from __future__ import annotations

import csv
from dataclasses import dataclass
from datetime import datetime
import hashlib
import math
from pathlib import Path
import subprocess
import time
from typing import TextIO


PLACEHOLDERS = {"", "unknown", "unspecified", "none", "uncalibrated"}


@dataclass
class AnalyzerSession:
    process: subprocess.Popen
    log_handle: TextIO
    command: list[str]
    run_id: str
    output_path: Path
    ready_path: Path
    stop_path: Path
    instrument_model: str
    instrument_serial: str
    calibration_ref: str
    adapter_sha256: str


def _epoch(raw: dict, line_number: int) -> float:
    try:
        if raw.get("epoch_s"):
            value = float(raw["epoch_s"])
        elif raw.get("timestamp"):
            value = datetime.fromisoformat(raw["timestamp"].replace("Z", "+00:00")).timestamp()
        else:
            raise ValueError("epoch_s or timestamp is required")
    except (TypeError, ValueError) as exc:
        raise ValueError(f"analyzer.csv:{line_number}: invalid timestamp: {exc}") from exc
    if not math.isfinite(value) or value <= 0:
        raise ValueError(f"analyzer.csv:{line_number}: timestamp must be finite and positive")
    return value


def validate_analyzer_csv(
    path: Path,
    run_id: str,
    instrument_model: str,
    instrument_serial: str,
    calibration_ref: str,
    minimum_samples: int = 2,
) -> dict:
    """Require a usable, attributed positive-discharge trace."""
    if not path.is_file():
        raise ValueError(f"analyzer adapter did not create {path}")
    rows = []
    with path.open(newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle)
        required = {
            "run_id", "current_ma", "instrument_model", "instrument_serial", "calibration_ref"
        }
        if reader.fieldnames is None or not required.issubset(reader.fieldnames):
            raise ValueError(f"analyzer.csv missing required columns: {sorted(required)}")
        if not ({"epoch_s", "timestamp"} & set(reader.fieldnames)):
            raise ValueError("analyzer.csv requires epoch_s or timestamp")
        if not ({"voltage_mv", "voltage_v"} & set(reader.fieldnames)):
            raise ValueError("analyzer.csv requires voltage_mv or voltage_v")
        for line_number, raw in enumerate(reader, start=2):
            if raw.get("run_id") != run_id:
                raise ValueError(
                    f"analyzer.csv:{line_number}: run_id {raw.get('run_id')!r} != {run_id!r}"
                )
            if (
                raw.get("instrument_model", "").strip() != instrument_model
                or raw.get("instrument_serial", "").strip() != instrument_serial
                or raw.get("calibration_ref", "").strip() != calibration_ref
            ):
                raise ValueError(f"analyzer.csv:{line_number}: instrument identity changed")
            epoch_s = _epoch(raw, line_number)
            try:
                current_ma = float(raw["current_ma"])
                voltage_mv = (
                    float(raw["voltage_mv"])
                    if raw.get("voltage_mv")
                    else float(raw["voltage_v"]) * 1000.0
                )
            except (TypeError, ValueError) as exc:
                raise ValueError(f"analyzer.csv:{line_number}: invalid sample: {exc}") from exc
            if not math.isfinite(current_ma) or current_ma < 0:
                raise ValueError(f"analyzer.csv:{line_number}: invalid positive-discharge current")
            if not math.isfinite(voltage_mv) or voltage_mv <= 0:
                raise ValueError(f"analyzer.csv:{line_number}: invalid voltage")
            rows.append((epoch_s, current_ma, voltage_mv))
    if len(rows) < minimum_samples:
        raise ValueError(f"analyzer.csv requires at least {minimum_samples} sample(s)")
    if any(current[0] <= previous[0] for previous, current in zip(rows, rows[1:])):
        raise ValueError("analyzer.csv timestamps must be strictly increasing")
    return {
        "passed": True,
        "run_id": run_id,
        "path": str(path.resolve()),
        "bytes": path.stat().st_size,
        "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
        "samples": len(rows),
        "first_epoch_s": rows[0][0],
        "last_epoch_s": rows[-1][0],
        "instrument_model": instrument_model,
        "instrument_serial": instrument_serial,
        "calibration_ref": calibration_ref,
    }


def start_analyzer_session(
    adapter: Path,
    out_dir: Path,
    run_id: str,
    instrument_model: str,
    instrument_serial: str,
    calibration_ref: str,
    ready_timeout_s: float,
) -> AnalyzerSession:
    for name, value in (
        ("instrument model", instrument_model),
        ("instrument serial", instrument_serial),
        ("calibration reference", calibration_ref),
    ):
        if value.strip().lower() in PLACEHOLDERS:
            raise ValueError(f"analyzer {name} is required")
    out_dir.mkdir(parents=True, exist_ok=True)
    output_path = out_dir / "analyzer.csv"
    ready_path = out_dir / "analyzer.ready"
    stop_path = out_dir / "analyzer.stop"
    for path in (output_path, ready_path, stop_path):
        path.unlink(missing_ok=True)
    command = [
        str(adapter.resolve()),
        "--run-id", run_id,
        "--output", str(output_path.resolve()),
        "--ready-file", str(ready_path.resolve()),
        "--stop-file", str(stop_path.resolve()),
        "--instrument-model", instrument_model,
        "--instrument-serial", instrument_serial,
        "--calibration-ref", calibration_ref,
    ]
    log_handle = (out_dir / "analyzer-adapter.log").open("w", encoding="utf-8")
    try:
        process = subprocess.Popen(
            command,
            stdout=log_handle,
            stderr=subprocess.STDOUT,
            text=True,
        )
        deadline = time.monotonic() + ready_timeout_s
        while time.monotonic() < deadline:
            if ready_path.is_file():
                validate_analyzer_csv(
                    output_path,
                    run_id,
                    instrument_model,
                    instrument_serial,
                    calibration_ref,
                    minimum_samples=1,
                )
                return AnalyzerSession(
                    process, log_handle, command, run_id, output_path, ready_path, stop_path,
                    instrument_model, instrument_serial, calibration_ref,
                    hashlib.sha256(adapter.read_bytes()).hexdigest(),
                )
            returncode = process.poll()
            if returncode is not None:
                raise RuntimeError(f"analyzer adapter exited before ready: {returncode}")
            time.sleep(0.1)
        raise TimeoutError(f"analyzer adapter did not become ready within {ready_timeout_s:g}s")
    except Exception:
        if "process" in locals() and process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5)
        log_handle.close()
        raise


def stop_analyzer_session(session: AnalyzerSession, stop_timeout_s: float) -> dict:
    try:
        session.stop_path.touch()
        try:
            returncode = session.process.wait(timeout=stop_timeout_s)
        except subprocess.TimeoutExpired as exc:
            session.process.terminate()
            try:
                session.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                session.process.kill()
                session.process.wait(timeout=5)
            raise TimeoutError(
                f"analyzer adapter did not stop within {stop_timeout_s:g}s"
            ) from exc
        if returncode != 0:
            raise RuntimeError(f"analyzer adapter exited with {returncode}")
    finally:
        session.log_handle.close()
    result = validate_analyzer_csv(
        session.output_path,
        session.run_id,
        session.instrument_model,
        session.instrument_serial,
        session.calibration_ref,
    )
    result["command"] = session.command
    result["adapter_sha256"] = session.adapter_sha256
    return result


def validate_capture_window(capture: dict, started_epoch_s: float, ended_epoch_s: float) -> None:
    """Require sampling to bracket the complete switched-battery interval."""
    first = float(capture.get("first_epoch_s", math.inf))
    last = float(capture.get("last_epoch_s", -math.inf))
    if not math.isfinite(started_epoch_s) or not math.isfinite(ended_epoch_s):
        raise ValueError("battery window timestamps are invalid")
    if ended_epoch_s <= started_epoch_s:
        raise ValueError("battery window did not advance")
    if first > started_epoch_s or last < ended_epoch_s:
        raise ValueError(
            "analyzer trace does not bracket VBUS-off window: "
            f"trace={first:.3f}..{last:.3f}, battery={started_epoch_s:.3f}..{ended_epoch_s:.3f}"
        )
    capture["battery_window_started_epoch_s"] = started_epoch_s
    capture["battery_window_ended_epoch_s"] = ended_epoch_s
    capture["battery_window_bracketed"] = True
