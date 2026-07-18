#!/usr/bin/env python3
"""Aggregate LunaSay battery QA artifacts into evidence and claim-safe reports.

Only samples inside a runner's VBUS-off window are attributed to that run.
Runtime estimates require a sufficiently long, monotonic battery-only segment;
short smoke tests remain useful as functional evidence but never become battery
life claims.
"""

from __future__ import annotations

import argparse
import csv
from datetime import datetime
import json
import math
from pathlib import Path
from statistics import mean


DEFAULT_MIN_ESTIMATE_HOURS = 1.0
DEFAULT_MIN_PERCENT_DROP = 2
ROOT = Path(__file__).resolve().parents[1]


def parse_time(value: str) -> float:
    return datetime.fromisoformat(value.replace("Z", "+00:00")).timestamp()


def load_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def load_events(path: Path) -> list[dict]:
    if not path.exists():
        return []
    return [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines() if line.strip()]


def linear_slope(points: list[tuple[float, float]]) -> float | None:
    """Return y units/hour for timestamped points, or None when underdetermined."""
    if len(points) < 2:
        return None
    origin = points[0][0]
    xs = [(x - origin) / 3600.0 for x, _ in points]
    ys = [y for _, y in points]
    x_bar = mean(xs)
    y_bar = mean(ys)
    denominator = sum((x - x_bar) ** 2 for x in xs)
    if denominator <= 0:
        return None
    return sum((x - x_bar) * (y - y_bar) for x, y in zip(xs, ys)) / denominator


def run_window(events: list[dict], summary: dict) -> tuple[float | None, float | None, bool]:
    off = next((parse_time(e["at"]) for e in events if e.get("kind") == "vbus_off"), None)
    on = next((parse_time(e["at"]) for e in events if e.get("kind") == "vbus_on"), None)
    shutdown = next(
        (parse_time(e["at"]) for e in events if e.get("kind") == "device_unreachable"),
        None,
    )
    if off is not None and on is None:
        elapsed = float(summary.get("elapsed_s", 0))
        on = off + elapsed if elapsed > 0 else None
    endpoint = summary.get("final_battery", {}).get("battery", {})
    endpoint_percent = endpoint.get("percent")
    endpoint_voltage_mv = endpoint.get("voltage_mv")
    inferred_confirmation = bool(
        shutdown is not None
        and (
            (isinstance(endpoint_percent, (int, float)) and endpoint_percent <= 5)
            or (isinstance(endpoint_voltage_mv, (int, float)) and endpoint_voltage_mv <= 3400)
        )
    )
    shutdown_confirmed = bool(summary.get("shutdown_confirmed", inferred_confirmation))
    return off, shutdown if shutdown is not None else on, shutdown_confirmed


def battery_samples(summary: dict, start: float | None, end: float | None) -> list[dict]:
    history = summary.get("final_battery", {}).get("history", [])
    if start is None or end is None:
        return []
    samples = []
    for sample in history:
        epoch = float(sample.get("epoch_s", 0))
        if not (start <= epoch <= end):
            continue
        if sample.get("vbus") or sample.get("charging") or not sample.get("battery_present"):
            continue
        samples.append(sample)
    return sorted(samples, key=lambda row: row["epoch_s"])


def classify(
    summary: dict,
    samples: list[dict],
    start: float | None,
    end: float | None,
    shutdown_observed: bool,
    charge_ready: bool,
    min_hours: float,
    min_drop: int,
    battery_mah: float | None,
) -> dict:
    duration_h = (end - start) / 3600.0 if start is not None and end is not None else 0.0
    percent_drop = 0
    voltage_drop_mv = 0
    if len(samples) >= 2:
        percent_drop = int(samples[0]["percent"]) - int(samples[-1]["percent"])
        voltage_drop_mv = int(samples[0]["voltage_mv"]) - int(samples[-1]["voltage_mv"])
    percent_slope = linear_slope([(float(s["epoch_s"]), float(s["percent"])) for s in samples])
    voltage_slope = linear_slope([(float(s["epoch_s"]), float(s["voltage_mv"])) for s in samples])
    percent_monotonic = all(
        int(current["percent"]) <= int(previous["percent"])
        for previous, current in zip(samples, samples[1:])
    )
    estimate_valid = (
        duration_h >= min_hours
        and len(samples) >= 3
        and percent_drop >= min_drop
        and percent_monotonic
        and percent_slope is not None
        and percent_slope < 0
    )
    full_runtime_h = 100.0 / -percent_slope if estimate_valid else None
    measured_runtime_h = duration_h if shutdown_observed else None
    runtime_for_current = measured_runtime_h if measured_runtime_h is not None else full_runtime_h
    average_current_ma = (
        battery_mah / runtime_for_current
        if battery_mah is not None and runtime_for_current is not None and runtime_for_current > 0
        else None
    )
    if not summary.get("passed", False):
        evidence = "failed"
    elif shutdown_observed and charge_ready:
        evidence = "measured-runtime"
    elif estimate_valid and charge_ready:
        evidence = "runtime-estimate"
    elif shutdown_observed or estimate_valid:
        evidence = "unqualified-runtime"
    elif len(samples) >= 1:
        evidence = "functional-only"
    else:
        evidence = "insufficient-samples"
    return {
        "duration_h": duration_h,
        "sample_count": len(samples),
        "percent_drop": percent_drop,
        "voltage_drop_mv": voltage_drop_mv,
        "percent_monotonic": percent_monotonic,
        "percent_per_hour": -percent_slope if percent_slope is not None else None,
        "voltage_drop_mv_per_hour": -voltage_slope if voltage_slope is not None else None,
        "projected_full_runtime_h": full_runtime_h,
        "measured_runtime_h": measured_runtime_h,
        "average_current_ma": average_current_ma,
        "shutdown_observed": shutdown_observed,
        "charge_ready": charge_ready,
        "evidence": evidence,
    }


def fmt(value: float | int | None, digits: int = 2) -> str:
    if value is None or (isinstance(value, float) and not math.isfinite(value)):
        return "—"
    if isinstance(value, int):
        return str(value)
    return f"{value:.{digits}f}"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--artifact-root", type=Path, default=Path("artifacts/qa"))
    parser.add_argument("--out-dir", type=Path, default=Path("artifacts/qa/lunasay-power-report"))
    parser.add_argument("--min-estimate-hours", type=float, default=DEFAULT_MIN_ESTIMATE_HOURS)
    parser.add_argument("--min-percent-drop", type=int, default=DEFAULT_MIN_PERCENT_DROP)
    parser.add_argument("--battery-mah", type=float, default=None,
                        help="labeled cell capacity; omit when unknown")
    parser.add_argument("--matrix", type=Path, default=ROOT / "config" / "lunasay_power_matrix.json")
    args = parser.parse_args()

    runs: list[dict] = []
    curves: list[dict] = []
    for summary_path in sorted(args.artifact_root.glob("lunasay-battery-*/summary.json")):
        summary = load_json(summary_path)
        article = summary.get("test_article", {})
        charge_gate = summary.get("charge_gate", {})
        charge_ready = bool(
            charge_gate.get("charge_terminated")
            and float(charge_gate.get("rested_s", -1)) >= float(charge_gate.get("required_rest_s", 0))
        )
        run_battery_mah = args.battery_mah
        if run_battery_mah is None and isinstance(article.get("battery_mah"), (int, float)):
            run_battery_mah = float(article["battery_mah"])
        events = load_events(summary_path.parent / "events.jsonl")
        start, end, shutdown_observed = run_window(events, summary)
        samples = battery_samples(summary, start, end)
        metrics = classify(
            summary,
            samples,
            start,
            end,
            shutdown_observed,
            charge_ready,
            args.min_estimate_hours,
            args.min_percent_drop,
            run_battery_mah,
        )
        workload_events = [
            event for event in events if event.get("kind") in ("voice_turn", "journal_segment")
        ]
        captured_audio_s = float(summary.get("captured_audio_s", 0))
        if captured_audio_s <= 0:
            captured_audio_s = sum(
                float(event.get("result", {}).get("capture_ms", 0)) / 1000.0
                for event in workload_events
            )
        capture_coverage_ratio = float(summary.get("capture_coverage_ratio", 0))
        if capture_coverage_ratio <= 0 and metrics["duration_h"] > 0:
            capture_coverage_ratio = min(1.0, captured_audio_s / (metrics["duration_h"] * 3600.0))
        run_id = summary_path.parent.name
        runs.append({
            "run_id": run_id,
            "scenario": summary.get("scenario", "unknown"),
            "workload": summary.get("workload", "unknown"),
            "passed": bool(summary.get("passed", False)),
            "unit_id": article.get("unit_id", "unknown"),
            "battery_id": article.get("battery_id", "unknown"),
            "battery_mah": run_battery_mah,
            "turns": int(summary.get("turns", 0)),
            "successful_turns": int(summary.get("successful_turns", 0)),
            "captured_audio_s": captured_audio_s,
            "capture_coverage_ratio": capture_coverage_ratio,
            **metrics,
        })
        for sample in samples:
            curves.append({
                "run_id": run_id,
                "scenario": summary.get("scenario", "unknown"),
                "workload": summary.get("workload", "unknown"),
                "elapsed_h": (float(sample["epoch_s"]) - start) / 3600.0,
                "percent": sample["percent"],
                "voltage_mv": sample["voltage_mv"],
            })

    args.out_dir.mkdir(parents=True, exist_ok=True)
    deep_runs: list[dict] = []
    for summary_path in sorted(args.artifact_root.glob("lunasay-deep-sleep-*/summary.json")):
        summary = load_json(summary_path)
        article = summary.get("test_article", {})
        charge_gate = summary.get("charge_gate", {})
        charge_ready = bool(
            charge_gate.get("charge_terminated")
            and float(charge_gate.get("rested_s", -1)) >= float(charge_gate.get("required_rest_s", 0))
        )
        sample = summary.get("wake_battery", {})
        sleep = sample.get("deep_sleep", {})
        battery = sample.get("battery", {})
        requested_s = float(sleep.get("requested_s", summary.get("duration_min", 0) * 60))
        duration_h = requested_s / 3600.0
        start_percent = sleep.get("start_percent")
        end_percent = battery.get("percent")
        drop = (
            float(start_percent) - float(end_percent)
            if isinstance(start_percent, (int, float)) and isinstance(end_percent, (int, float))
            else 0.0
        )
        valid_projection = bool(
            summary.get("passed")
            and duration_h >= args.min_estimate_hours
            and drop >= args.min_percent_drop
        )
        projected_h = 100.0 * duration_h / drop if valid_projection else None
        run_battery_mah = args.battery_mah
        if run_battery_mah is None and isinstance(article.get("battery_mah"), (int, float)):
            run_battery_mah = float(article["battery_mah"])
        deep_runs.append({
            "run_id": summary_path.parent.name,
            "runner": "deep-sleep",
            "passed": bool(summary.get("passed", False)),
            "unit_id": article.get("unit_id", "unknown"),
            "battery_id": article.get("battery_id", "unknown"),
            "duration_h": duration_h,
            "percent_drop": drop,
            "projected_full_runtime_h": projected_h,
            "average_current_ma": (
                run_battery_mah / projected_h
                if run_battery_mah is not None and projected_h is not None and projected_h > 0
                else None
            ),
            "evidence": "runtime-estimate" if valid_projection and charge_ready else (
                "unqualified-runtime" if valid_projection else
                "functional-only" if summary.get("passed") else "failed"
            ),
        })
    run_fields = [
        "run_id", "scenario", "workload", "passed", "unit_id", "battery_id", "battery_mah",
        "turns", "successful_turns",
        "captured_audio_s", "capture_coverage_ratio",
        "duration_h", "sample_count", "percent_drop", "voltage_drop_mv", "percent_monotonic",
        "percent_per_hour", "voltage_drop_mv_per_hour", "projected_full_runtime_h",
        "measured_runtime_h", "average_current_ma", "shutdown_observed", "charge_ready", "evidence",
    ]
    with (args.out_dir / "runs.csv").open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=run_fields)
        writer.writeheader()
        writer.writerows(runs)
    curve_fields = ["run_id", "scenario", "workload", "elapsed_h", "percent", "voltage_mv"]
    with (args.out_dir / "curves.csv").open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=curve_fields)
        writer.writeheader()
        writer.writerows(curves)

    lines = [
        "# LunaSay power evidence report",
        "",
        "Generated from hub-controlled QA artifacts. Battery claims are withheld unless a run has "
        f"at least {args.min_estimate_hours:g} hour(s), three battery-only samples, and "
        f"a {args.min_percent_drop}% monotonic drop, and a completed full-charge/rest gate.",
        "" if args.battery_mah is None else f"Average current uses the labeled {args.battery_mah:g} mAh cell capacity.",
        "",
        "| Scenario | Workload | Duration | Samples | Drop | Rate | Projected | Measured | Avg current | Evidence |",
        "|---|---|---:|---:|---:|---:|---:|---:|---:|---|",
    ]
    for run in runs:
        lines.append(
            f"| {run['scenario']} | {run['workload']} | {fmt(run['duration_h'])} h | "
            f"{run['sample_count']} | {run['percent_drop']}% | "
            f"{fmt(run['percent_per_hour'])}%/h | {fmt(run['projected_full_runtime_h'])} h | "
            f"{fmt(run['measured_runtime_h'])} h | {fmt(run['average_current_ma'])} mA | "
            f"{run['evidence']} |"
        )
    if not runs:
        lines.append("| — | — | — | — | — | — | — | — | — | no completed runs |")
    voice_runs = [run for run in runs if run["workload"] in ("conversation", "journal")]
    if voice_runs:
        lines.extend([
            "",
            "## Voice workload evidence",
            "",
            "| Workload | Scenario | Segments/turns | Successful | Captured audio | Coverage |",
            "|---|---|---:|---:|---:|---:|",
        ])
        for run in voice_runs:
            lines.append(
                f"| {run['workload']} | {run['scenario']} | {run['turns']} | "
                f"{run['successful_turns']} | {fmt(run['captured_audio_s'], 1)} s | "
                f"{fmt(run['capture_coverage_ratio'] * 100.0, 1)}% |"
            )
    if deep_runs:
        lines.extend([
            "",
            "## True deep-sleep evidence",
            "",
            "| Unit | Duration | Drop | Projected runtime | Avg current | Evidence |",
            "|---|---:|---:|---:|---:|---|",
        ])
        for run in deep_runs:
            lines.append(
                f"| {run['unit_id']} | {fmt(run['duration_h'])} h | "
                f"{fmt(run['percent_drop'])}% | {fmt(run['projected_full_runtime_h'])} h | "
                f"{fmt(run['average_current_ma'])} mA | {run['evidence']} |"
            )

    if args.matrix.exists():
        matrix = load_json(args.matrix)
        rank = {"failed": 0, "insufficient-samples": 1, "unqualified-runtime": 2,
                "functional-only": 2,
                "runtime-estimate": 3, "measured-runtime": 4}
        lines.extend([
            "",
            "## Required-matrix coverage",
            "",
            "| Test | Display | Radio | Workload | Best evidence | Units | Release gate |",
            "|---|---|---|---|---|---:|---|",
        ])
        for test in matrix.get("tests", []):
            if test.get("runner") == "deep-sleep":
                candidates = deep_runs
            else:
                candidates = [
                    run for run in runs
                    if run["scenario"] == test.get("scenario") and run["workload"] == test.get("workload")
                ]
            best = max(candidates, key=lambda run: rank.get(run["evidence"], -1)) if candidates else None
            best_evidence = best["evidence"] if best is not None else "missing"
            measured_units = {
                run.get("unit_id", "unknown") for run in candidates
                if run.get("evidence") == "measured-runtime" and run.get("unit_id") != "unknown"
            }
            required_units = int(matrix.get("release_gate", {}).get("units_required", 2))
            gate = "ready" if len(measured_units) >= required_units else "open"
            lines.append(
                f"| {test['id']} | {test.get('display', '—')} | {test.get('radio', '—')} | "
                f"{test.get('workload', test.get('runner', '—'))} | {best_evidence} | "
                f"{len(measured_units)} | {gate} |"
            )
    lines.extend([
        "",
        "## Claim status",
        "",
        "No Kickstarter battery-life claim should be published from a `functional-only` smoke test. "
        "A projected runtime is engineering evidence; the final public claim requires at least one "
        "release-candidate run to shutdown in each advertised mode and a repeat run on a second unit.",
        "",
    ])
    (args.out_dir / "report.md").write_text("\n".join(lines), encoding="utf-8")
    print(f"lunasay_power_report: wrote {len(runs)} runs and {len(curves)} samples to {args.out_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
