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
import hashlib
from html import escape
import json
import math
from pathlib import Path
from statistics import mean, median


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


def load_analyzer_rows(paths: list[Path]) -> list[dict]:
    """Load battery-path analyzer CSVs; positive current means discharge."""
    rows: list[dict] = []
    seen: dict[tuple[str, float], tuple[float, float, Path]] = {}
    for path in paths:
        with path.open(newline="", encoding="utf-8") as handle:
            for line_number, raw in enumerate(csv.DictReader(handle), start=2):
                if not raw.get("run_id") or not raw.get("current_ma"):
                    raise ValueError(f"{path}:{line_number}: run_id and current_ma are required")
                try:
                    if raw.get("epoch_s"):
                        epoch_s = float(raw["epoch_s"])
                    elif raw.get("timestamp"):
                        epoch_s = parse_time(raw["timestamp"])
                    else:
                        raise ValueError("epoch_s or timestamp is required")
                    current_ma = float(raw["current_ma"])
                    if raw.get("voltage_mv"):
                        voltage_mv = float(raw["voltage_mv"])
                    elif raw.get("voltage_v"):
                        voltage_mv = float(raw["voltage_v"]) * 1000.0
                    else:
                        raise ValueError("voltage_mv or voltage_v is required")
                except (TypeError, ValueError) as exc:
                    raise ValueError(f"{path}:{line_number}: invalid analyzer sample: {exc}") from exc
                if not math.isfinite(epoch_s) or epoch_s <= 0:
                    raise ValueError(f"{path}:{line_number}: timestamp must be finite and positive")
                if not math.isfinite(current_ma) or current_ma < 0:
                    raise ValueError(
                        f"{path}:{line_number}: current_ma must be finite and non-negative"
                    )
                if not math.isfinite(voltage_mv) or voltage_mv <= 0:
                    raise ValueError(
                        f"{path}:{line_number}: voltage must be finite and positive"
                    )
                key = (raw["run_id"], epoch_s)
                electrical = (current_ma, voltage_mv)
                if key in seen:
                    previous_current, previous_voltage, previous_path = seen[key]
                    if electrical != (previous_current, previous_voltage):
                        raise ValueError(
                            f"{path}:{line_number}: conflicting duplicate timestamp for "
                            f"{raw['run_id']}; first seen in {previous_path}"
                        )
                    continue
                seen[key] = (current_ma, voltage_mv, path)
                rows.append({
                    "run_id": raw["run_id"],
                    "epoch_s": epoch_s,
                    "current_ma": current_ma,
                    "voltage_mv": voltage_mv,
                })
    return rows


def direct_power_metrics(rows: list[dict]) -> dict | None:
    rows = sorted(rows, key=lambda row: row["epoch_s"])
    if len(rows) < 2 or rows[-1]["epoch_s"] <= rows[0]["epoch_s"]:
        return None
    charge_mah = 0.0
    energy_mwh = 0.0
    gaps_s: list[float] = []
    for previous, current in zip(rows, rows[1:]):
        gap_s = current["epoch_s"] - previous["epoch_s"]
        hours = gap_s / 3600.0
        if hours <= 0:
            continue
        gaps_s.append(gap_s)
        mean_current = (previous["current_ma"] + current["current_ma"]) / 2.0
        previous_power_mw = previous["voltage_mv"] * previous["current_ma"] / 1000.0
        current_power_mw = current["voltage_mv"] * current["current_ma"] / 1000.0
        charge_mah += mean_current * hours
        energy_mwh += (previous_power_mw + current_power_mw) / 2.0 * hours
    duration_h = (rows[-1]["epoch_s"] - rows[0]["epoch_s"]) / 3600.0
    return {
        "analyzer_samples": len(rows),
        "analyzer_duration_h": duration_h,
        "analyzer_median_gap_s": median(gaps_s) if gaps_s else None,
        "analyzer_max_gap_s": max(gaps_s) if gaps_s else None,
        "median_current_ma": median(row["current_ma"] for row in rows),
        "average_current_ma": charge_mah / duration_h if duration_h > 0 else None,
        "peak_current_ma": max(row["current_ma"] for row in rows),
        "charge_mah": charge_mah,
        "energy_wh": energy_mwh / 1000.0,
        "current_basis": "direct-battery-analyzer",
    }


def analyzer_shutdown_epoch(
    rows: list[dict],
    threshold_ma: float,
    sustain_s: float,
) -> float | None:
    """Find a sustained battery-path current collapse after observable active load."""
    ordered = sorted(rows, key=lambda row: row["epoch_s"])
    if len(ordered) < 4:
        return None
    active_seen = False
    for index, row in enumerate(ordered):
        if row["current_ma"] > threshold_ma:
            active_seen = True
            continue
        if not active_seen:
            continue
        tail = ordered[index:]
        if len(tail) < 3 or tail[-1]["epoch_s"] - row["epoch_s"] < sustain_s:
            continue
        if all(sample["current_ma"] <= threshold_ma for sample in tail):
            return float(row["epoch_s"])
    return None


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


def release_build_key(run: dict) -> tuple[str, str] | None:
    """Return a claim-safe firmware/harness pair, rejecting unknown or dirty builds."""
    firmware = str(run.get("firmware_build", "")).strip()
    harness = str(run.get("harness_build", "")).strip()
    if (
        not firmware
        or not harness
        or firmware == "unknown"
        or harness == "unknown"
        or "dirty" in firmware.lower()
        or "dirty" in harness.lower()
    ):
        return None
    return firmware, harness


def largest_release_cohort(runs: list[dict]) -> list[dict]:
    """Keep only the same clean firmware/harness cohort with the most physical units."""
    cohorts: dict[tuple[str, str], list[dict]] = {}
    for run in runs:
        key = release_build_key(run)
        if key is not None:
            cohorts.setdefault(key, []).append(run)
    if not cohorts:
        return []
    ranked = sorted(
        cohorts.items(),
        key=lambda item: (
            -len({run.get("unit_id") for run in item[1] if run.get("unit_id") != "unknown"}),
            item[0],
        ),
    )
    return ranked[0][1]


def workload_gate_passes(run: dict, test: dict) -> bool:
    return bool(
        int(run.get("successful_turns", 0)) >= int(test.get("minimum_successful_turns", 0))
        and float(run.get("capture_coverage_ratio", 0))
        >= float(test.get("minimum_capture_coverage_ratio", 0))
    )


def events_fully_inside(events: list[dict], start_epoch_s: float, end_epoch_s: float) -> list[dict]:
    """Return completed workload events whose entire measured operation is metered."""
    inside: list[dict] = []
    for event in events:
        try:
            completed_epoch_s = parse_time(str(event["at"]))
            wall_s = float(event["wall_s"])
        except (KeyError, TypeError, ValueError):
            continue
        if wall_s < 0:
            continue
        if completed_epoch_s - wall_s >= start_epoch_s and completed_epoch_s <= end_epoch_s:
            inside.append(event)
    return inside


def write_curves_svg(curves: list[dict], path: Path) -> None:
    """Write dependency-free percent/voltage discharge plots for all runs."""
    width, height = 1200, 680
    left, right = 78, 260
    top, panel_h, gap = 72, 230, 76
    plot_w = width - left - right
    colors = ("#63d8ff", "#ffb86c", "#bd93f9", "#50fa7b", "#ff79c6", "#f1fa8c", "#8be9fd")
    run_ids = list(dict.fromkeys(str(row["run_id"]) for row in curves))
    max_h = max((float(row["elapsed_h"]) for row in curves), default=1.0)
    max_h = max(max_h, 0.25)
    voltages = [float(row["voltage_mv"]) for row in curves]
    v_min = min(voltages, default=3200.0)
    v_max = max(voltages, default=4300.0)
    if v_max - v_min < 100:
        midpoint = (v_min + v_max) / 2.0
        v_min, v_max = midpoint - 50.0, midpoint + 50.0
    else:
        v_min -= 25.0
        v_max += 25.0

    def x_pos(hours: float) -> float:
        return left + plot_w * hours / max_h

    def y_percent(value: float) -> float:
        return top + panel_h * (1.0 - value / 100.0)

    voltage_top = top + panel_h + gap

    def y_voltage(value: float) -> float:
        return voltage_top + panel_h * (1.0 - (value - v_min) / (v_max - v_min))

    svg = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
        '<rect width="100%" height="100%" fill="#071018"/>',
        '<style>text{font-family:ui-monospace,SFMono-Regular,monospace;fill:#dbeaf2} .grid{stroke:#24404d;stroke-width:1} .axis{stroke:#7fa0af;stroke-width:1.3}</style>',
        '<text x="78" y="34" font-size="22" font-weight="700">LunaSay battery discharge</text>',
        '<text x="78" y="56" font-size="12" fill="#8da8b5">Battery-only samples retained by firmware; elapsed time begins at VBUS removal.</text>',
    ]
    for panel_top, label in ((top, "Fuel gauge (%)"), (voltage_top, "Battery voltage (mV)")):
        svg.append(f'<line class="axis" x1="{left}" y1="{panel_top}" x2="{left}" y2="{panel_top + panel_h}"/>')
        svg.append(f'<line class="axis" x1="{left}" y1="{panel_top + panel_h}" x2="{left + plot_w}" y2="{panel_top + panel_h}"/>')
        svg.append(f'<text x="{left}" y="{panel_top - 10}" font-size="14">{label}</text>')
        for tick in range(5):
            x = left + plot_w * tick / 4
            elapsed = max_h * tick / 4
            svg.append(f'<line class="grid" x1="{x:.1f}" y1="{panel_top}" x2="{x:.1f}" y2="{panel_top + panel_h}"/>')
            svg.append(f'<text x="{x:.1f}" y="{panel_top + panel_h + 19}" text-anchor="middle" font-size="11">{elapsed:.1f}h</text>')
    for tick in range(5):
        pct = 100 - tick * 25
        y = top + panel_h * tick / 4
        svg.append(f'<line class="grid" x1="{left}" y1="{y:.1f}" x2="{left + plot_w}" y2="{y:.1f}"/>')
        svg.append(f'<text x="{left - 10}" y="{y + 4:.1f}" text-anchor="end" font-size="11">{pct}</text>')
        mv = v_max - (v_max - v_min) * tick / 4
        vy = voltage_top + panel_h * tick / 4
        svg.append(f'<line class="grid" x1="{left}" y1="{vy:.1f}" x2="{left + plot_w}" y2="{vy:.1f}"/>')
        svg.append(f'<text x="{left - 10}" y="{vy + 4:.1f}" text-anchor="end" font-size="11">{mv:.0f}</text>')
    for index, run_id in enumerate(run_ids):
        color = colors[index % len(colors)]
        rows = [row for row in curves if str(row["run_id"]) == run_id]
        percent_points = " ".join(
            f'{x_pos(float(row["elapsed_h"])):.1f},{y_percent(float(row["percent"])):.1f}' for row in rows
        )
        voltage_points = " ".join(
            f'{x_pos(float(row["elapsed_h"])):.1f},{y_voltage(float(row["voltage_mv"])):.1f}' for row in rows
        )
        if len(rows) > 1:
            svg.append(f'<polyline points="{percent_points}" fill="none" stroke="{color}" stroke-width="2.5"/>')
            svg.append(f'<polyline points="{voltage_points}" fill="none" stroke="{color}" stroke-width="2.5"/>')
        for row in rows:
            svg.append(f'<circle cx="{x_pos(float(row["elapsed_h"])):.1f}" cy="{y_percent(float(row["percent"])):.1f}" r="3" fill="{color}"/>')
            svg.append(f'<circle cx="{x_pos(float(row["elapsed_h"])):.1f}" cy="{y_voltage(float(row["voltage_mv"])):.1f}" r="3" fill="{color}"/>')
        legend_y = 18 + index * 16
        svg.append(f'<rect x="965" y="{legend_y - 9}" width="10" height="10" fill="{color}"/>')
        svg.append(f'<text x="981" y="{legend_y}" font-size="10">{escape(run_id)}</text>')
    if not curves:
        svg.append('<text x="500" y="340" text-anchor="middle" font-size="18">No completed battery-only curves yet</text>')
    svg.append('</svg>')
    path.write_text("\n".join(svg) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--artifact-root", type=Path, default=Path("artifacts/qa"))
    parser.add_argument("--out-dir", type=Path, default=Path("artifacts/qa/lunasay-power-report"))
    parser.add_argument("--min-estimate-hours", type=float, default=DEFAULT_MIN_ESTIMATE_HOURS)
    parser.add_argument("--min-percent-drop", type=int, default=DEFAULT_MIN_PERCENT_DROP)
    parser.add_argument("--battery-mah", type=float, default=None,
                        help="labeled cell capacity; omit when unknown")
    parser.add_argument("--analyzer-csv", type=Path, action="append", default=[],
                        help="battery-path CSV: run_id, epoch_s|timestamp, current_ma, voltage_mv|voltage_v")
    parser.add_argument("--shutdown-current-threshold-ma", type=float, default=0.2,
                        help="maximum battery current considered electrically off")
    parser.add_argument("--shutdown-current-sustain-s", type=float, default=300.0,
                        help="required continuous near-zero tail for analyzer shutdown inference")
    parser.add_argument("--analyzer-max-gap-s", type=float, default=60.0,
                        help="largest accepted gap between direct-current samples")
    parser.add_argument("--matrix", type=Path, default=ROOT / "config" / "lunasay_power_matrix.json")
    args = parser.parse_args()
    if (
        args.shutdown_current_threshold_ma < 0
        or args.shutdown_current_sustain_s <= 0
        or args.analyzer_max_gap_s <= 0
    ):
        raise SystemExit(
            "error: analyzer shutdown threshold must be non-negative; sustain and maximum gap "
            "must be positive"
        )
    analyzer_rows = load_analyzer_rows(args.analyzer_csv)
    analyzer_sources = [
        {
            "path": str(path.resolve()),
            "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
            "bytes": path.stat().st_size,
        }
        for path in args.analyzer_csv
    ]

    runs: list[dict] = []
    curves: list[dict] = []
    for summary_path in sorted(args.artifact_root.glob("lunasay-battery-*/summary.json")):
        summary = load_json(summary_path)
        scenario_evidence = summary.get("scenario_evidence", {})
        scenario_verified = bool(scenario_evidence.get("passed", False))
        validated_summary = dict(summary)
        validated_summary["passed"] = bool(summary.get("passed", False) and scenario_verified)
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
        shutdown_basis = "runner-confirmed" if shutdown_observed else "none"
        window_analyzer_rows = [
            row for row in analyzer_rows
            if row["run_id"] == summary_path.parent.name
            and start is not None and end is not None
            and start <= row["epoch_s"] <= end
        ]
        recovery = summary.get("shutdown_evidence", {})
        radio_off_idle = bool(
            summary.get("workload") == "idle"
            and summary.get("scenario") in ("full-offline", "dim-offline", "sleep-offline")
        )
        inferred_shutdown = analyzer_shutdown_epoch(
            window_analyzer_rows,
            args.shutdown_current_threshold_ma,
            args.shutdown_current_sustain_s,
        )
        if (
            not shutdown_observed
            and radio_off_idle
            and inferred_shutdown is not None
            and recovery.get("reboot_confirmed")
            and recovery.get("poweron_reset")
            and recovery.get("pmu_under_voltage")
        ):
            end = inferred_shutdown
            shutdown_observed = True
            shutdown_basis = "analyzer-current-collapse+pmu-undervoltage-reset"
        samples = battery_samples(summary, start, end)
        metrics = classify(
            validated_summary,
            samples,
            start,
            end,
            shutdown_observed,
            charge_ready,
            args.min_estimate_hours,
            args.min_percent_drop,
            run_battery_mah,
        )
        direct_rows = [
            row for row in window_analyzer_rows
            if end is not None and row["epoch_s"] <= end
        ]
        direct_metrics = direct_power_metrics(direct_rows)
        metrics.update({
            "analyzer_samples": 0,
            "analyzer_duration_h": 0.0,
            "analyzer_coverage_ratio": 0.0,
            "analyzer_median_gap_s": None,
            "analyzer_max_gap_s": None,
            "median_current_ma": None,
            "peak_current_ma": None,
            "charge_mah": None,
            "energy_wh": None,
            "current_basis": "capacity-derived" if metrics["average_current_ma"] is not None else "unknown",
        })
        if direct_metrics is not None:
            direct_metrics["analyzer_coverage_ratio"] = min(
                1.0,
                direct_metrics["analyzer_duration_h"] / metrics["duration_h"]
                if metrics["duration_h"] > 0 else 0.0,
            )
            if direct_metrics["analyzer_coverage_ratio"] < 0.95:
                direct_metrics["current_basis"] = "direct-battery-analyzer-partial"
            elif float(direct_metrics.get("analyzer_max_gap_s") or math.inf) > args.analyzer_max_gap_s:
                direct_metrics["current_basis"] = "direct-battery-analyzer-gapped"
            metrics.update(direct_metrics)
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
        successful_turns = int(summary.get("successful_turns", 0))
        accepted_captures = int(summary.get("accepted_captures", summary.get("turns", 0)))
        ble_probes = int(summary.get("ble_probes", 0))
        successful_ble_probes = int(summary.get("successful_ble_probes", 0))
        ble_config_roundtrips = int(summary.get("ble_config_roundtrips", 0))
        metered_events: list[dict] = []
        if direct_rows:
            analyzer_start_epoch_s = min(float(row["epoch_s"]) for row in direct_rows)
            analyzer_end_epoch_s = max(float(row["epoch_s"]) for row in direct_rows)
            metered_events = events_fully_inside(
                events,
                analyzer_start_epoch_s,
                analyzer_end_epoch_s,
            )
        metered_successful_turns = sum(
            int(event.get("kind") == "voice_turn" and bool(event.get("passed")))
            for event in metered_events
        )
        metered_captured_audio_s = sum(
            float(event.get("result", {}).get("capture_ms", 0)) / 1000.0
            for event in metered_events
            if event.get("kind") == "journal_segment"
            and (
                bool(event.get("accepted"))
                or float(event.get("result", {}).get("capture_ms", 0)) > 0
            )
        )
        metered_ble_config_roundtrips = sum(
            int(event.get("kind") == "ble_config_roundtrip")
            for event in metered_events
        )
        energy_wh = metrics.get("energy_wh")
        energy_per_unit_mwh = None
        if isinstance(energy_wh, (int, float)) and metrics["current_basis"] == "direct-battery-analyzer":
            if summary.get("workload") == "conversation" and metered_successful_turns > 0:
                energy_per_unit_mwh = float(energy_wh) * 1000.0 / metered_successful_turns
            elif summary.get("workload") == "journal" and metered_captured_audio_s > 0:
                energy_per_unit_mwh = float(energy_wh) * 1000.0 / (metered_captured_audio_s / 60.0)
            elif summary.get("workload") == "ble-config" and metered_ble_config_roundtrips > 0:
                energy_per_unit_mwh = float(energy_wh) * 1000.0 / metered_ble_config_roundtrips
        run_id = summary_path.parent.name
        runs.append({
            "run_id": run_id,
            "scenario": summary.get("scenario", "unknown"),
            "workload": summary.get("workload", "unknown"),
            "passed": bool(validated_summary.get("passed", False)),
            "scenario_evidence_passed": scenario_verified,
            "scenario_counter_passed": bool(scenario_evidence.get("counter_passed", False)),
            "scenario_history_passed": bool(scenario_evidence.get("history_passed", False)),
            "unit_id": article.get("unit_id", "unknown"),
            "battery_id": article.get("battery_id", "unknown"),
            "battery_mah": run_battery_mah,
            "battery_photo_sha256": article.get("battery_photo_sha256"),
            "firmware_build": article.get("firmware_build", "unknown"),
            "harness_build": article.get("harness_build", "unknown"),
            "shutdown_basis": shutdown_basis,
            "turns": int(summary.get("turns", 0)),
            "accepted_captures": accepted_captures,
            "successful_turns": successful_turns,
            "ble_probes": ble_probes,
            "successful_ble_probes": successful_ble_probes,
            "ble_config_roundtrips": ble_config_roundtrips,
            "captured_audio_s": captured_audio_s,
            "capture_coverage_ratio": capture_coverage_ratio,
            "metered_successful_turns": metered_successful_turns,
            "metered_captured_audio_s": metered_captured_audio_s,
            "metered_ble_config_roundtrips": metered_ble_config_roundtrips,
            "energy_per_unit_mwh": energy_per_unit_mwh,
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
    (args.out_dir / "analyzer_sources.json").write_text(
        json.dumps(analyzer_sources, indent=2) + "\n",
        encoding="utf-8",
    )
    report_config = {
        "artifact_root": str(args.artifact_root.resolve()),
        "matrix": str(args.matrix.resolve()),
        "matrix_sha256": (
            hashlib.sha256(args.matrix.read_bytes()).hexdigest() if args.matrix.is_file() else None
        ),
        "minimum_estimate_hours": args.min_estimate_hours,
        "minimum_percent_drop": args.min_percent_drop,
        "battery_mah_override": args.battery_mah,
        "analyzer_max_gap_s": args.analyzer_max_gap_s,
        "shutdown_current_threshold_ma": args.shutdown_current_threshold_ma,
        "shutdown_current_sustain_s": args.shutdown_current_sustain_s,
    }
    (args.out_dir / "report_config.json").write_text(
        json.dumps(report_config, indent=2) + "\n",
        encoding="utf-8",
    )
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
        wake_source = summary.get("wake_source", "timer")
        requested_s = float(sleep.get("requested_s", summary.get("duration_min", 0) * 60))
        observed_s = summary.get("network_wake_after_s")
        duration_s = float(observed_s) if isinstance(observed_s, (int, float)) else requested_s
        duration_h = duration_s / 3600.0
        start_percent = sleep.get("start_percent")
        end_percent = battery.get("percent")
        drop = (
            float(start_percent) - float(end_percent)
            if isinstance(start_percent, (int, float)) and isinstance(end_percent, (int, float))
            else 0.0
        )
        valid_projection = bool(
            summary.get("passed")
            and wake_source == "timer"
            and duration_h >= args.min_estimate_hours
            and drop >= args.min_percent_drop
        )
        projected_h = 100.0 * duration_h / drop if valid_projection else None
        run_battery_mah = args.battery_mah
        if run_battery_mah is None and isinstance(article.get("battery_mah"), (int, float)):
            run_battery_mah = float(article["battery_mah"])
        deep_events = load_events(summary_path.parent / "events.jsonl")
        deep_start = next(
            (parse_time(event["at"]) for event in deep_events if event.get("kind") == "vbus_off"),
            None,
        )
        deep_end = next(
            (parse_time(event["at"]) for event in deep_events if event.get("kind") == "network_wake"),
            None,
        )
        if deep_end is None:
            deep_end = next(
                (parse_time(event["at"]) for event in deep_events if event.get("kind") == "vbus_on"),
                None,
            )
        deep_analyzer_rows = [
            row for row in analyzer_rows
            if row["run_id"] == summary_path.parent.name
            and deep_start is not None and deep_end is not None
            and deep_start <= row["epoch_s"] <= deep_end
        ]
        deep_direct = direct_power_metrics(deep_analyzer_rows)
        deep_power = {
            "analyzer_samples": 0,
            "analyzer_duration_h": 0.0,
            "analyzer_coverage_ratio": 0.0,
            "analyzer_median_gap_s": None,
            "analyzer_max_gap_s": None,
            "median_current_ma": None,
            "average_current_ma": None,
            "peak_current_ma": None,
            "charge_mah": None,
            "energy_wh": None,
            "current_basis": "unknown",
        }
        if deep_direct is not None:
            deep_direct["analyzer_coverage_ratio"] = min(
                1.0,
                deep_direct["analyzer_duration_h"] / duration_h if duration_h > 0 else 0.0,
            )
            if deep_direct["analyzer_coverage_ratio"] < 0.95:
                deep_direct["current_basis"] = "direct-battery-analyzer-partial"
            elif float(deep_direct.get("analyzer_max_gap_s") or math.inf) > args.analyzer_max_gap_s:
                deep_direct["current_basis"] = "direct-battery-analyzer-gapped"
            deep_power.update(deep_direct)
        direct_runtime_h = (
            run_battery_mah / deep_power["average_current_ma"]
            if run_battery_mah is not None
            and deep_power["current_basis"] == "direct-battery-analyzer"
            and isinstance(deep_power["average_current_ma"], (int, float))
            and deep_power["average_current_ma"] > 0
            else None
        )
        if direct_runtime_h is not None:
            projected_h = direct_runtime_h
            valid_projection = bool(summary.get("passed"))
        elif deep_power["average_current_ma"] is None and run_battery_mah is not None and projected_h:
            deep_power["average_current_ma"] = run_battery_mah / projected_h
            deep_power["current_basis"] = "capacity-derived"
        deep_runs.append({
            "run_id": summary_path.parent.name,
            "runner": "deep-sleep",
            "scenario": "true-deep-sleep",
            "workload": wake_source,
            "wake_source": wake_source,
            "passed": bool(summary.get("passed", False)),
            "unit_id": article.get("unit_id", "unknown"),
            "battery_id": article.get("battery_id", "unknown"),
            "battery_mah": run_battery_mah,
            "battery_photo_sha256": article.get("battery_photo_sha256"),
            "firmware_build": article.get("firmware_build", "unknown"),
            "harness_build": article.get("harness_build", "unknown"),
            "duration_h": duration_h,
            "percent_drop": drop,
            "projected_full_runtime_h": projected_h,
            **deep_power,
            "evidence": "runtime-estimate" if valid_projection and charge_ready else (
                "unqualified-runtime" if valid_projection else
                "functional-only" if summary.get("passed") else "failed"
            ),
        })
    run_fields = [
        "run_id", "scenario", "workload", "passed", "scenario_evidence_passed",
        "scenario_counter_passed", "scenario_history_passed", "unit_id", "battery_id", "battery_mah",
        "battery_photo_sha256",
        "firmware_build", "harness_build",
        "turns", "accepted_captures", "successful_turns", "ble_probes", "successful_ble_probes",
        "ble_config_roundtrips",
        "captured_audio_s", "capture_coverage_ratio", "metered_successful_turns",
        "metered_captured_audio_s", "metered_ble_config_roundtrips", "energy_per_unit_mwh",
        "duration_h", "sample_count", "percent_drop", "voltage_drop_mv", "percent_monotonic",
        "percent_per_hour", "voltage_drop_mv_per_hour", "projected_full_runtime_h",
        "measured_runtime_h", "median_current_ma", "average_current_ma", "peak_current_ma", "charge_mah", "energy_wh",
        "current_basis", "analyzer_samples", "analyzer_duration_h", "analyzer_coverage_ratio",
        "analyzer_median_gap_s", "analyzer_max_gap_s",
        "shutdown_observed", "shutdown_basis", "charge_ready", "evidence",
    ]
    with (args.out_dir / "runs.csv").open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=run_fields)
        writer.writeheader()
        writer.writerows(runs)
    deep_fields = [
        "run_id", "scenario", "workload", "wake_source", "passed", "unit_id", "battery_id",
        "battery_mah", "battery_photo_sha256", "firmware_build", "harness_build",
        "duration_h", "percent_drop", "projected_full_runtime_h",
        "median_current_ma", "average_current_ma", "peak_current_ma", "charge_mah", "energy_wh",
        "current_basis", "analyzer_samples", "analyzer_duration_h", "analyzer_coverage_ratio",
        "analyzer_median_gap_s", "analyzer_max_gap_s",
        "evidence",
    ]
    with (args.out_dir / "deep_sleep_runs.csv").open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=deep_fields)
        writer.writeheader()
        writer.writerows(deep_runs)
    curve_fields = ["run_id", "scenario", "workload", "elapsed_h", "percent", "voltage_mv"]
    with (args.out_dir / "curves.csv").open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=curve_fields)
        writer.writeheader()
        writer.writerows(curves)
    write_curves_svg(curves, args.out_dir / "curves.svg")

    lines = [
        "# LunaSay power evidence report",
        "",
        "Generated from hub-controlled QA artifacts. Battery claims are withheld unless a run has "
        f"at least {args.min_estimate_hours:g} hour(s), three battery-only samples, and "
        f"a {args.min_percent_drop}% monotonic drop, and a completed full-charge/rest gate.",
        "Analyzer input paths, sizes, and SHA-256 hashes are recorded in `analyzer_sources.json`; "
        "all report thresholds and the matrix hash are recorded in `report_config.json`.",
        "",
        "![Battery discharge curves](curves.svg)",
        "" if args.battery_mah is None else f"Average current uses the labeled {args.battery_mah:g} mAh cell capacity.",
        "",
        "| Scenario | Workload | Duration | Samples | Drop | Rate | Projected | Measured | Avg current | Current basis | Energy | Shutdown basis | Evidence |",
        "|---|---|---:|---:|---:|---:|---:|---:|---:|---|---:|---|---|",
    ]
    for run in runs:
        lines.append(
            f"| {run['scenario']} | {run['workload']} | {fmt(run['duration_h'])} h | "
            f"{run['sample_count']} | {run['percent_drop']}% | "
            f"{fmt(run['percent_per_hour'])}%/h | {fmt(run['projected_full_runtime_h'])} h | "
            f"{fmt(run['measured_runtime_h'])} h | {fmt(run['average_current_ma'])} mA | "
            f"{run['current_basis']} | {fmt(run['energy_wh'], 3)} Wh | "
            f"{run['shutdown_basis']} | {run['evidence']} |"
        )
    if not runs:
        lines.append("| — | — | — | — | — | — | — | — | — | — | — | — | no completed runs |")
    voice_runs = [run for run in runs if run["workload"] in ("conversation", "journal")]
    direct_runs = [
        run for run in [*runs, *deep_runs]
        if str(run["current_basis"]).startswith("direct-battery-analyzer")
    ]
    if direct_runs:
        lines.extend([
            "",
            "## Direct battery-path power evidence",
            "",
            "| Scenario | Workload | Samples | Coverage | Median gap | Max gap | Median | Average | Peak | Charge | Energy |",
            "|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|",
        ])
        for run in direct_runs:
            lines.append(
                f"| {run['scenario']} | {run['workload']} | {run['analyzer_samples']} | "
                f"{fmt(run['analyzer_coverage_ratio'] * 100.0, 1)}% | "
                f"{fmt(run['analyzer_median_gap_s'], 1)} s | "
                f"{fmt(run['analyzer_max_gap_s'], 1)} s | "
                f"{fmt(run['median_current_ma'])} mA | {fmt(run['average_current_ma'])} mA | "
                f"{fmt(run['peak_current_ma'])} mA | {fmt(run['charge_mah'])} mAh | "
                f"{fmt(run['energy_wh'], 3)} Wh |"
            )
    if voice_runs:
        lines.extend([
            "",
            "## Voice workload evidence",
            "",
            "| Workload | Scenario | Attempts | Accepted captures | Successful | Captured audio | Coverage | Metered denominator | Direct energy/unit |",
            "|---|---|---:|---:|---:|---:|---:|---:|---:|",
        ])
        for run in voice_runs:
            metered_denominator = (
                f"{fmt(run['metered_captured_audio_s'], 1)} s"
                if run["workload"] == "journal"
                else f"{run['metered_successful_turns']} turns"
            )
            lines.append(
                f"| {run['workload']} | {run['scenario']} | {run['turns']} | "
                f"{run['accepted_captures']} | {run['successful_turns']} | "
                f"{fmt(run['captured_audio_s'], 1)} s | "
                f"{fmt(run['capture_coverage_ratio'] * 100.0, 1)}% | "
                f"{metered_denominator} | "
                f"{fmt(run['energy_per_unit_mwh'], 2)} mWh |"
            )
    ble_runs = [run for run in runs if run["workload"] in ("ble", "ble-config")]
    if ble_runs:
        lines.extend([
            "",
            "## BLE workload evidence",
            "",
            "| Workload | Scenario | Probes | Successful | Success | Set roundtrips | Metered sets | Direct mWh/set interval |",
            "|---|---|---:|---:|---:|---:|---:|---:|",
        ])
        for run in ble_runs:
            success_ratio = (
                run["successful_ble_probes"] / run["ble_probes"]
                if run["ble_probes"] > 0 else 0.0
            )
            config_energy = run["energy_per_unit_mwh"] if run["workload"] == "ble-config" else None
            lines.append(
                f"| {run['workload']} | {run['scenario']} | {run['ble_probes']} | "
                f"{run['successful_ble_probes']} | {fmt(success_ratio * 100.0, 1)}% | "
                f"{run['ble_config_roundtrips']} | {run['metered_ble_config_roundtrips']} | "
                f"{fmt(config_energy, 2)} |"
            )
    if deep_runs:
        lines.extend([
            "",
            "## True deep-sleep evidence",
            "",
            "| Unit | Wake | Duration | Drop | Projected runtime | Avg current | Current basis | Evidence |",
            "|---|---|---:|---:|---:|---:|---|---|",
        ])
        for run in deep_runs:
            lines.append(
                f"| {run['unit_id']} | {run['wake_source']} | {fmt(run['duration_h'])} h | "
                f"{fmt(run['percent_drop'])}% | {fmt(run['projected_full_runtime_h'])} h | "
                f"{fmt(run['average_current_ma'])} mA | {run['current_basis']} | {run['evidence']} |"
            )

    claim_rows: list[dict] = []
    matrix_open_count = 0
    if args.matrix.exists():
        matrix = load_json(args.matrix)
        rank = {"failed": 0, "insufficient-samples": 1, "unqualified-runtime": 2,
                "functional-only": 2,
                "runtime-estimate": 3, "measured-runtime": 4}
        lines.extend([
            "",
            "## Required-matrix coverage",
            "",
            "| Test | Display | Radio | Workload | Best evidence | Required basis | Units | Release gate |",
            "|---|---|---|---|---|---|---:|---|",
        ])
        release_gate = matrix.get("release_gate", {})
        require_direct_current = bool(release_gate.get("require_direct_current", False))
        require_labeled_capacity = bool(release_gate.get("require_labeled_capacity", False))
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
            release_basis = test.get("release_basis", "measured-runtime")
            if release_basis == "direct-projection":
                qualifying = [
                    run for run in candidates
                    if run.get("passed")
                    and run.get("wake_source") == "timer"
                    and run.get("current_basis") == "direct-battery-analyzer"
                    and isinstance(run.get("projected_full_runtime_h"), (int, float))
                    and isinstance(run.get("battery_mah"), (int, float))
                    and bool(run.get("battery_photo_sha256"))
                ]
            elif release_basis == "direct-workload":
                qualifying = [
                    run for run in candidates
                    if run.get("passed")
                    and run.get("charge_ready")
                    and run.get("current_basis") == "direct-battery-analyzer"
                    and float(run.get("analyzer_coverage_ratio", 0)) >= 0.95
                    and int(run.get("successful_ble_probes", 0)) > 0
                    and int(run.get("ble_config_roundtrips", 0)) > 0
                    and (
                        not require_labeled_capacity
                        or (
                            isinstance(run.get("battery_mah"), (int, float))
                            and bool(run.get("battery_photo_sha256"))
                        )
                    )
                ]
            else:
                qualifying = [
                    run for run in candidates
                    if run.get("evidence") == "measured-runtime"
                    and workload_gate_passes(run, test)
                    and (
                        not require_direct_current
                        or (
                            run.get("current_basis") == "direct-battery-analyzer"
                            and float(run.get("analyzer_coverage_ratio", 0)) >= 0.95
                        )
                    )
                    and (
                        not require_labeled_capacity
                        or (
                            isinstance(run.get("battery_mah"), (int, float))
                            and bool(run.get("battery_photo_sha256"))
                        )
                    )
                ]
            if release_basis == "direct-projection":
                qualifying = [
                    run for run in qualifying
                    if any(
                        gate_run.get("passed")
                        and gate_run.get("wake_source") == "gpio0"
                        and gate_run.get("unit_id") == run.get("unit_id")
                        and release_build_key(gate_run) == release_build_key(run)
                        for gate_run in deep_runs
                    )
                ]
            qualifying = largest_release_cohort(qualifying)
            qualifying_units = {
                run.get("unit_id", "unknown") for run in qualifying
                if run.get("unit_id") != "unknown"
            }
            required_units = int(release_gate.get("units_required", 2))
            gate = "ready" if len(qualifying_units) >= required_units else "open"
            matrix_open_count += int(gate != "ready")
            required_basis_label = (
                "measured-runtime+direct-current"
                if release_basis == "measured-runtime" and require_direct_current
                else release_basis
            )
            if int(test.get("minimum_successful_turns", 0)) > 0:
                required_basis_label += f"+≥{int(test['minimum_successful_turns'])}-turns"
            if float(test.get("minimum_capture_coverage_ratio", 0)) > 0:
                required_basis_label += (
                    f"+≥{float(test['minimum_capture_coverage_ratio']) * 100:g}%-capture"
                )
            lines.append(
                f"| {test['id']} | {test.get('display', '—')} | {test.get('radio', '—')} | "
                f"{test.get('workload', test.get('runner', '—'))} | {best_evidence} | "
                f"{required_basis_label} | "
                f"{len(qualifying_units)} | {gate} |"
            )
            if gate == "ready" and release_basis == "measured-runtime":
                minimum_h = min(float(run["measured_runtime_h"]) for run in qualifying)
                step_h = 0.5 if minimum_h >= 2.0 else 0.25
                claim_h = math.floor(minimum_h / step_h) * step_h
                if claim_h > 0:
                    workload_claim = str(test.get("workload", "tested"))
                    if float(test.get("minimum_capture_coverage_ratio", 0)) > 0:
                        workload_claim += (
                            f" at ≥{float(test['minimum_capture_coverage_ratio']) * 100:g}% "
                            "audio-capture duty cycle"
                        )
                    claim_rows.append({
                        "test": test["id"],
                        "basis": "measured full-to-shutdown + direct current",
                        "units": len(qualifying_units),
                        "draft": (
                            f"At least {claim_h:g} hours with the display at "
                            f"{test.get('display', 'the tested level')}, {test.get('radio', 'tested radio')} "
                            f"and {workload_claim} workload."
                        ),
                    })
            elif gate == "ready" and release_basis == "direct-projection":
                minimum_h = min(float(run["projected_full_runtime_h"]) for run in qualifying)
                if minimum_h >= 24.0:
                    conservative = math.floor(minimum_h / 24.0)
                    duration = f"approximately {conservative:g} days"
                else:
                    conservative = math.floor(minimum_h * 2.0) / 2.0
                    duration = f"approximately {conservative:g} hours"
                claim_rows.append({
                    "test": test["id"],
                    "basis": "labeled capacity + direct sleep current",
                    "units": len(qualifying_units),
                    "draft": f"Projected deep-sleep battery life is {duration}.",
                })
    limitations: list[str] = []
    if not runs and not deep_runs:
        limitations.append("No completed hub-controlled runs were found.")
    if any(not run.get("charge_ready", False) for run in runs):
        limitations.append("One or more active-mode runs skipped or failed the full-charge/30-minute-rest gate.")
    if any(not run.get("scenario_evidence_passed", False) for run in runs):
        limitations.append(
            "One or more active-mode runs lack verified battery-only display/radio scenario evidence."
        )
    if not analyzer_rows:
        limitations.append("No inline battery-path analyzer trace is present; direct current, mAh, and Wh remain unknown.")
    if any(run.get("current_basis") == "direct-battery-analyzer-gapped" for run in [*runs, *deep_runs]):
        limitations.append(
            f"At least one analyzer trace exceeds the {args.analyzer_max_gap_s:g}-second maximum "
            "sample gap and is excluded from release claims."
        )
    if any(
        run.get("battery_mah") is None or not run.get("battery_photo_sha256")
        for run in [*runs, *deep_runs]
    ):
        limitations.append(
            "At least one test article lacks a labeled cell capacity or hashed label photo; "
            "its release gate remains open."
        )
    if any(release_build_key(run) is None for run in [*runs, *deep_runs]):
        limitations.append(
            "At least one artifact has unknown or dirty firmware/harness provenance and is excluded "
            "from release claims."
        )
    if not any(run.get("evidence") == "measured-runtime" for run in runs):
        limitations.append("No qualified active-mode run has yet reached confirmed automatic low-voltage shutdown.")
    if not any(run.get("passed") and run.get("wake_source") == "timer" for run in deep_runs):
        limitations.append("Timed deep-sleep wake has not yet passed on hardware.")
    if not any(run.get("passed") and run.get("wake_source") == "gpio0" for run in deep_runs):
        limitations.append("Physical BOOT/GPIO0 deep-sleep wake has not yet passed on hardware.")
    measured_cohort = largest_release_cohort(
        [run for run in runs if run.get("evidence") == "measured-runtime"]
    )
    measured_units = {
        run.get("unit_id") for run in measured_cohort
        if run.get("unit_id") not in (None, "unknown")
    }
    if len(measured_units) < 2:
        limitations.append(
            "The two-release-candidate-unit repetition gate on one clean firmware/harness build "
            "is still open."
        )
    if matrix_open_count:
        limitations.append(f"{matrix_open_count} required matrix release gate(s) remain open.")
    lines.extend(["", "## Limitations", ""])
    lines.extend(f"- {limitation}" for limitation in limitations)
    lines.extend(["", "## Claim status", ""])
    if claim_rows:
        lines.extend([
            "The following conservative drafts have passed their per-mode evidence gates:",
            "",
            "| Test | Units | Basis | Conservative draft |",
            "|---|---:|---|---|",
        ])
        for claim in claim_rows:
            lines.append(
                f"| {claim['test']} | {claim['units']} | {claim['basis']} | {claim['draft']} |"
            )
        lines.append("")
    else:
        lines.extend(["No Kickstarter battery-life claim is evidence-ready yet.", ""])
    lines.extend([
        "Never publish a battery-life claim from a `functional-only` smoke test. Active-mode claims "
        "require full-to-shutdown runs on two release-candidate units. Deep-sleep wording remains "
        "explicitly projected even when it is backed by labeled capacity and direct current traces.",
        "",
    ])
    (args.out_dir / "report.md").write_text("\n".join(lines), encoding="utf-8")
    print(f"lunasay_power_report: wrote {len(runs)} runs and {len(curves)} samples to {args.out_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
