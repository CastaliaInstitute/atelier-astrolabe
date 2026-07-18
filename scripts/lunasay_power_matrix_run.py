#!/usr/bin/env python3
"""Run the LunaSay power matrix sequentially with resumable, attributable artifacts."""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
import fcntl
import hashlib
import json
import math
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_MATRIX = ROOT / "config" / "lunasay_power_matrix.json"


def save_json(path: Path, value: dict) -> None:
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")
    temporary.replace(path)


def common_args(args: argparse.Namespace, out_dir: Path, test: dict) -> list[str]:
    values = [
        "--ip", args.ip,
        "--hub-location", args.hub_location,
        "--hub-port", str(args.hub_port),
        "--uhubctl", args.uhubctl,
        "--out-dir", str(out_dir),
        "--unit-id", args.unit_id,
        "--hardware-revision", args.hardware_revision,
        "--battery-id", args.battery_id,
        "--rest-min", str(args.rest_min),
        "--charge-ready-timeout-min", str(args.charge_ready_timeout_min),
        "--qualification-matrix-sha256", args.matrix_sha256,
        "--qualification-test-id", str(test["id"]),
    ]
    if args.port:
        values += ["--port", args.port]
    if args.battery_mah is not None:
        values += ["--battery-mah", str(args.battery_mah)]
    if args.battery_photo is not None:
        values += ["--battery-photo", str(args.battery_photo.resolve())]
    if args.ambient_c is not None:
        values += ["--ambient-c", str(args.ambient_c)]
    if args.battery_cycle_count is not None:
        values += ["--battery-cycle-count", str(args.battery_cycle_count)]
    if args.allow_not_ready:
        values.append("--allow-not-ready")
    return values


def command_for(test: dict, args: argparse.Namespace, out_dir: Path) -> list[str]:
    duration_min = int(test["duration_min"])
    if test.get("runner") == "deep-sleep":
        return [
            sys.executable,
            str(ROOT / "scripts" / "lunasay_deep_sleep_validate.py"),
            "--duration-min", str(duration_min),
            "--wake-source", str(test.get("wake_source", "timer")),
            *common_args(args, out_dir, test),
        ]
    command = [
        sys.executable,
        str(ROOT / "scripts" / "lunasay_battery_validate.py"),
        "--scenario", str(test["scenario"]),
        "--workload", str(test["workload"]),
        "--duration-min", str(duration_min),
        *common_args(args, out_dir, test),
    ]
    optional = {
        "capture_ms": "--capture-ms",
        "turn_interval_s": "--turn-interval-s",
        "journal_gap_s": "--journal-gap-s",
        "turn_timeout_s": "--turn-timeout-s",
        "say_rate": "--say-rate",
        "say_volume": "--say-volume",
        "ble_probe_interval_s": "--ble-probe-interval-s",
        "ble_probe_timeout_s": "--ble-probe-timeout-s",
        "ble_config_write_interval_s": "--ble-config-write-interval-s",
    }
    for key, flag in optional.items():
        if key in test:
            command += [flag, str(test[key])]
    return command


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--matrix", type=Path, default=DEFAULT_MATRIX)
    parser.add_argument("--artifact-root", type=Path, default=ROOT / "artifacts" / "qa")
    parser.add_argument("--state", type=Path, default=ROOT / "artifacts" / "qa" / "lunasay-power-matrix-state.json")
    parser.add_argument("--only", default="", help="comma-separated test IDs")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--allow-not-ready", action="store_true",
                        help="smoke-test only; skip charge termination/rest")
    parser.add_argument("--ip", default="192.168.86.72")
    parser.add_argument("--port", default="")
    parser.add_argument("--hub-location", default="0-1.3")
    parser.add_argument("--hub-port", type=int, default=1)
    parser.add_argument("--uhubctl", default="/opt/homebrew/bin/uhubctl")
    parser.add_argument("--unit-id", required=True)
    parser.add_argument("--hardware-revision", default="")
    parser.add_argument("--battery-id", required=True)
    parser.add_argument("--battery-mah", type=float, default=None)
    parser.add_argument("--battery-photo", type=Path, default=None)
    parser.add_argument("--battery-cycle-count", type=int, default=None)
    parser.add_argument("--ambient-c", type=float, default=None)
    parser.add_argument("--rest-min", type=float, default=30.0)
    parser.add_argument("--charge-ready-timeout-min", type=float, default=360.0)
    args = parser.parse_args()
    if args.battery_mah is not None and (
        not math.isfinite(args.battery_mah) or args.battery_mah <= 0
    ):
        raise SystemExit("error: --battery-mah must be finite and positive")
    if args.ambient_c is not None and not math.isfinite(args.ambient_c):
        raise SystemExit("error: --ambient-c must be finite")
    if (
        not math.isfinite(args.rest_min)
        or not math.isfinite(args.charge_ready_timeout_min)
        or args.rest_min < 0
        or args.charge_ready_timeout_min <= 0
    ):
        raise SystemExit("error: charge rest/timeout must be finite and non-negative/positive")
    if args.battery_cycle_count is not None and args.battery_cycle_count < 0:
        raise SystemExit("error: --battery-cycle-count must be non-negative")
    if args.battery_photo is not None and not args.battery_photo.is_file():
        raise SystemExit(f"error: battery label photo not found: {args.battery_photo}")
    if not args.allow_not_ready and (
        args.battery_mah is None
        or args.battery_photo is None
        or not args.hardware_revision.strip()
        or args.ambient_c is None
        or args.unit_id.strip().lower() in ("", "unknown", "unspecified")
        or args.battery_id.strip().lower() in ("", "unknown", "unspecified", "unlabeled")
    ):
        raise SystemExit(
            "error: qualified matrix runs require --battery-mah, --battery-photo, "
            "--hardware-revision, --ambient-c, and identified --unit-id/--battery-id"
        )
    battery_photo_sha256 = (
        hashlib.sha256(args.battery_photo.read_bytes()).hexdigest()
        if args.battery_photo is not None else None
    )

    matrix_bytes = args.matrix.read_bytes()
    matrix_sha256 = hashlib.sha256(matrix_bytes).hexdigest()
    args.matrix_sha256 = matrix_sha256
    matrix = json.loads(matrix_bytes)
    harness_build = subprocess.check_output(
        ["git", "describe", "--always", "--dirty"], cwd=ROOT, text=True
    ).strip()
    if not args.allow_not_ready and (not harness_build or "dirty" in harness_build.lower()):
        raise SystemExit(
            "error: qualified matrix runs require a clean committed harness build; "
            f"current build is {harness_build!r}"
        )
    tests = matrix.get("tests", [])
    selected = {item for item in args.only.split(",") if item}
    if selected:
        known = {str(test["id"]) for test in tests}
        unknown = selected - known
        if unknown:
            raise SystemExit(f"error: unknown test IDs: {', '.join(sorted(unknown))}")
        tests = [test for test in tests if str(test["id"]) in selected]
    if not tests:
        raise SystemExit("error: no matrix tests selected")

    args.artifact_root.mkdir(parents=True, exist_ok=True)
    args.state.parent.mkdir(parents=True, exist_ok=True)
    lock_path = args.state.with_suffix(args.state.suffix + ".lock")
    with lock_path.open("w", encoding="utf-8") as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError as exc:
            raise SystemExit(f"error: matrix runner already holds {lock_path}") from exc

        if args.state.exists():
            state = json.loads(args.state.read_text(encoding="utf-8"))
            if state.get("unit_id") != args.unit_id or state.get("battery_id") != args.battery_id:
                raise SystemExit("error: state belongs to a different unit/battery; choose another --state")
            if (
                state.get("hardware_revision") != (args.hardware_revision.strip() or "unknown")
                or state.get("battery_cycle_count") != args.battery_cycle_count
                or state.get("ambient_c") != args.ambient_c
            ):
                raise SystemExit(
                    "error: test-article provenance differs from this state; choose a new --state"
                )
            if (
                state.get("battery_mah") != args.battery_mah
                or state.get("battery_photo_sha256") != battery_photo_sha256
            ):
                raise SystemExit(
                    "error: battery capacity/photo differs from this state; choose a new --state"
                )
            if state.get("matrix_sha256") != matrix_sha256:
                raise SystemExit(
                    "error: matrix changed since this state was created; choose a new --state"
                )
            if state.get("harness_build") != harness_build:
                raise SystemExit(
                    "error: source/harness build changed since this state was created; "
                    "choose a new --state"
                )
        else:
            state = {
                "schema": 5,
                "matrix": str(args.matrix.resolve()),
                "matrix_schema": matrix.get("schema"),
                "matrix_sha256": matrix_sha256,
                "harness_build": harness_build,
                "unit_id": args.unit_id,
                "hardware_revision": args.hardware_revision.strip() or "unknown",
                "battery_id": args.battery_id,
                "battery_mah": args.battery_mah,
                "battery_photo_sha256": battery_photo_sha256,
                "battery_cycle_count": args.battery_cycle_count,
                "ambient_c": args.ambient_c,
                "firmware_build": None,
                "created_at": datetime.now(timezone.utc).isoformat(),
                "completed": [],
                "attempts": [],
            }
        completed = set(state.get("completed", []))

        for test in tests:
            test_id = str(test["id"])
            if test_id in completed:
                print(f"matrix: SKIP completed {test_id}", flush=True)
                continue
            stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
            prefix = "lunasay-deep-sleep" if test.get("runner") == "deep-sleep" else "lunasay-battery"
            out_dir = args.artifact_root / f"{prefix}-{test_id}-{stamp}"
            command = command_for(test, args, out_dir)
            print("matrix: RUN " + test_id, flush=True)
            print("matrix: CMD " + " ".join(command), flush=True)
            if args.dry_run:
                continue
            started_at = datetime.now(timezone.utc).isoformat()
            result = subprocess.run(command, cwd=ROOT)
            summary_path = out_dir / "summary.json"
            child_summary = (
                json.loads(summary_path.read_text(encoding="utf-8"))
                if summary_path.is_file() else {}
            )
            article = child_summary.get("test_article", {})
            child_firmware = article.get("firmware_build")
            child_harness = article.get("harness_build")
            attempt = {
                "test_id": test_id,
                "started_at": started_at,
                "finished_at": datetime.now(timezone.utc).isoformat(),
                "returncode": result.returncode,
                "out_dir": str(out_dir),
                "test": test,
                "command": command,
                "firmware_build": child_firmware,
                "harness_build": child_harness,
            }
            state.setdefault("attempts", []).append(attempt)
            state_error = None
            if result.returncode == 0 and (
                not article.get("firmware_provenance_complete")
                or str(article.get("firmware_variant", "")).lower() != "lunasay"
            ):
                state_error = "passing child run did not report a complete LunaSay binary identity"
            elif result.returncode == 0 and child_firmware in (None, "", "unknown"):
                state_error = "passing child run did not report a firmware build"
            elif result.returncode == 0 and child_harness != harness_build:
                state_error = (
                    f"child harness build {child_harness!r} does not match matrix {harness_build!r}"
                )
            elif result.returncode == 0 and state.get("firmware_build") not in (None, child_firmware):
                state_error = (
                    f"device firmware changed from {state.get('firmware_build')!r} "
                    f"to {child_firmware!r}"
                )
            if state_error is not None:
                attempt["state_error"] = state_error
                save_json(args.state, state)
                print(f"matrix: STOP {test_id}: {state_error}", flush=True)
                return 3
            if result.returncode == 0:
                state["firmware_build"] = child_firmware
                completed.add(test_id)
                state["completed"] = sorted(completed)
            save_json(args.state, state)
            if result.returncode != 0:
                print(f"matrix: STOP {test_id} failed with {result.returncode}", flush=True)
                return result.returncode

        if args.dry_run:
            print(f"matrix: dry-run listed {len(tests)} test(s)")
        else:
            state["finished_at"] = datetime.now(timezone.utc).isoformat()
            save_json(args.state, state)
            print(f"matrix: COMPLETE {len(completed)} test(s)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
