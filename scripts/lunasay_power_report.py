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


def file_manifest(path: Path) -> dict:
    return {
        "path": str(path.resolve()),
        "bytes": path.stat().st_size,
        "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
    }


def analyzer_capture_bound(summary_path: Path, capture: object) -> bool:
    """Bind a validated adapter result to this run's exact analyzer.csv bytes."""
    if not isinstance(capture, dict):
        return False
    adapter_sha256 = str(capture.get("adapter_sha256", "")).lower()
    expected_path = summary_path.parent / "analyzer.csv"
    try:
        captured_path = Path(str(capture["path"]))
        captured_sha256 = str(capture["sha256"]).lower()
    except (KeyError, TypeError, ValueError):
        return False
    return bool(
        capture.get("passed") is True
        and capture.get("battery_window_bracketed") is True
        and capture.get("run_id") == summary_path.parent.name
        and expected_path.is_file()
        and captured_path.name == "analyzer.csv"
        and len(captured_sha256) == 64
        and hashlib.sha256(expected_path.read_bytes()).hexdigest() == captured_sha256
        and len(adapter_sha256) == 64
        and all(char in "0123456789abcdef" for char in adapter_sha256)
    )


def analyzer_paths(explicit: list[Path], artifact_root: Path) -> list[Path]:
    """Combine explicit imports with conventional per-run captures without duplicates."""
    candidates = [*explicit, *sorted(artifact_root.glob("lunasay-*/analyzer.csv"))]
    unique: dict[Path, Path] = {}
    for path in candidates:
        resolved = path.resolve()
        unique.setdefault(resolved, path)
    return list(unique.values())


def load_analyzer_rows(paths: list[Path]) -> list[dict]:
    """Load battery-path analyzer CSVs; positive current means discharge."""
    rows: list[dict] = []
    seen: dict[tuple[str, float], tuple[float, float, str, str, str, Path]] = {}
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
                instrument_model = str(raw.get("instrument_model", "")).strip()
                instrument_serial = str(raw.get("instrument_serial", "")).strip()
                calibration_ref = str(raw.get("calibration_ref", "")).strip()
                sample_identity = (
                    current_ma,
                    voltage_mv,
                    instrument_model,
                    instrument_serial,
                    calibration_ref,
                )
                if key in seen:
                    *previous_identity, previous_path = seen[key]
                    if sample_identity != tuple(previous_identity):
                        raise ValueError(
                            f"{path}:{line_number}: conflicting duplicate timestamp for "
                            f"{raw['run_id']}; first seen in {previous_path}"
                        )
                    continue
                seen[key] = (*sample_identity, path)
                rows.append({
                    "run_id": raw["run_id"],
                    "epoch_s": epoch_s,
                    "current_ma": current_ma,
                    "voltage_mv": voltage_mv,
                    "instrument_model": instrument_model,
                    "instrument_serial": instrument_serial,
                    "calibration_ref": calibration_ref,
                    "source_path": str(path.resolve()),
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
    instrument_identities = {
        (
            str(row.get("instrument_model", "")).strip(),
            str(row.get("instrument_serial", "")).strip(),
            str(row.get("calibration_ref", "")).strip(),
        )
        for row in rows
    }
    instrument_model = instrument_serial = calibration_ref = None
    analyzer_provenance_complete = False
    if len(instrument_identities) == 1:
        identity = next(iter(instrument_identities))
        placeholders = {"", "unknown", "unspecified", "none", "uncalibrated"}
        if all(value.lower() not in placeholders for value in identity):
            instrument_model, instrument_serial, calibration_ref = identity
            analyzer_provenance_complete = True
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
        "analyzer_instrument_model": instrument_model,
        "analyzer_instrument_serial": instrument_serial,
        "analyzer_calibration_ref": calibration_ref,
        "analyzer_provenance_complete": analyzer_provenance_complete,
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


def reconcile_shutdown_endpoint(
    end: float | None,
    shutdown_observed: bool,
    inferred_shutdown: float | None,
    recovery: dict,
) -> tuple[float | None, bool, str]:
    """Prefer an earlier electrical endpoint only with independent PMU recovery proof."""
    electrical_endpoint_valid = bool(
        inferred_shutdown is not None
        and end is not None
        and (not shutdown_observed or inferred_shutdown < end)
        and recovery.get("reboot_confirmed")
        and recovery.get("poweron_reset")
        and recovery.get("pmu_under_voltage")
    )
    if electrical_endpoint_valid:
        return (
            inferred_shutdown,
            True,
            "analyzer-current-collapse+pmu-undervoltage-reset",
        )
    return end, shutdown_observed, "runner-confirmed" if shutdown_observed else "none"


def reconcile_scenario_evidence(evidence: dict, battery_duration_s: float) -> dict:
    """Recompute retained scenario coverage at an analyzer-confirmed endpoint."""
    reconciled = json.loads(json.dumps(evidence)) if isinstance(evidence, dict) else {}
    try:
        seconds = reconciled["seconds"]
        expected = reconciled["expected"]
        battery_s = float(seconds["firmware_battery"])
        mode_s = float(seconds["display_mode"])
        wifi_s = float(seconds["wifi"])
        ble_s = float(seconds["ble"])
    except (KeyError, TypeError, ValueError, OverflowError):
        return reconciled
    if (
        not math.isfinite(battery_duration_s)
        or battery_duration_s <= 0
        or not all(math.isfinite(value) and value >= 0 for value in (battery_s, mode_s, wifi_s, ble_s))
    ):
        return reconciled
    coverage_ratio = battery_s / battery_duration_s
    mode_ratio = mode_s / battery_s if battery_s > 0 else 0.0
    wifi_ratio = wifi_s / battery_s if battery_s > 0 else 0.0
    ble_ratio = ble_s / battery_s if battery_s > 0 else 0.0
    wifi_expected = bool(expected.get("wifi"))
    ble_expected = bool(expected.get("ble"))
    old_checks = reconciled.get("counter_checks", {})
    checks = {
        "scenario_name": bool(old_checks.get("scenario_name")),
        "battery_counter_coverage": coverage_ratio >= 0.80,
        "display_mode_dominant": mode_ratio >= 0.90,
        "wifi_state": wifi_ratio >= 0.75 if wifi_expected else (wifi_s <= 5 or wifi_ratio <= 0.05),
        "ble_state": ble_ratio >= 0.75 if ble_expected else (ble_s <= 5 or ble_ratio <= 0.05),
    }
    reconciled["counter_checks"] = checks
    reconciled["counter_passed"] = all(checks.values())
    reconciled["passed"] = bool(reconciled["counter_passed"] or reconciled.get("history_passed"))
    reconciled["seconds"] = {**seconds, "runner_battery": round(battery_duration_s, 3)}
    reconciled["ratios"] = {
        "counter_coverage": round(coverage_ratio, 4),
        "display_mode": round(mode_ratio, 4),
        "wifi": round(wifi_ratio, 4),
        "ble": round(ble_ratio, 4),
    }
    reconciled["endpoint_reconciled"] = True
    return reconciled


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
    start_percent = int(samples[0]["percent"]) if samples else None
    end_percent = int(samples[-1]["percent"]) if samples else None
    start_voltage_mv = int(samples[0]["voltage_mv"]) if samples else None
    end_voltage_mv = int(samples[-1]["voltage_mv"]) if samples else None
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
        "start_percent": start_percent,
        "end_percent": end_percent,
        "start_voltage_mv": start_voltage_mv,
        "end_voltage_mv": end_voltage_mv,
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


def release_build_key(run: dict) -> tuple[str, str, str] | None:
    """Return a claim-safe firmware/harness/hardware cohort key."""
    firmware = str(run.get("firmware_build", "")).strip()
    harness = str(run.get("harness_build", "")).strip()
    hardware_revision = str(run.get("hardware_revision", "")).strip()
    firmware_variant = str(run.get("firmware_variant", "")).strip()
    firmware_elf_sha256 = str(run.get("firmware_elf_sha256", "")).strip().lower()
    if (
        not firmware
        or not harness
        or not hardware_revision
        or firmware == "unknown"
        or harness == "unknown"
        or hardware_revision.lower() in ("unknown", "unspecified")
        or firmware_variant.lower() != "lunasay"
        or len(firmware_elf_sha256) != 64
        or any(char not in "0123456789abcdef" for char in firmware_elf_sha256)
        or not run.get("firmware_provenance_complete")
        or "dirty" in firmware.lower()
        or "dirty" in harness.lower()
    ):
        return None
    return firmware, harness, hardware_revision


def known_text(value: object) -> bool:
    return str(value or "").strip().lower() not in (
        "", "unknown", "unspecified", "unlabeled", "none"
    )


def charge_gate_passes(
    charge_gate: dict,
    minimum_start_voltage_mv: int,
    minimum_rest_s: float,
) -> bool:
    """Revalidate the recorded full-charge sample instead of trusting its flag."""
    try:
        rested_s = float(charge_gate["rested_s"])
        required_rest_s = float(charge_gate["required_rest_s"])
        recorded_minimum_mv = int(charge_gate["minimum_voltage_mv"])
        recorded_maximum_mv = int(charge_gate["maximum_voltage_mv"])
        status = charge_gate["status"]
        voltage_mv = int(status["mv"])
        percent = int(status["percent"])
    except (KeyError, TypeError, ValueError, OverflowError):
        return False
    return bool(
        charge_gate.get("charge_terminated") is True
        and math.isfinite(rested_s)
        and math.isfinite(required_rest_s)
        and required_rest_s >= minimum_rest_s
        and rested_s >= required_rest_s
        and recorded_minimum_mv >= minimum_start_voltage_mv
        and recorded_maximum_mv <= 4400
        and recorded_minimum_mv <= voltage_mv <= recorded_maximum_mv
        and status.get("docked") == "yes"
        and status.get("vbus") == "yes"
        and status.get("battery") == "present"
        and status.get("charging") == "no"
        and percent >= 99
    )


def test_article_gate_passes(
    run: dict,
    require_labeled_capacity: bool,
    require_hardware_revision: bool,
    require_ambient_temperature: bool,
    ambient_c_min: float | None = None,
    ambient_c_max: float | None = None,
) -> bool:
    ambient_c = run.get("ambient_c")
    ambient_valid = bool(
        isinstance(ambient_c, (int, float))
        and not isinstance(ambient_c, bool)
        and math.isfinite(float(ambient_c))
        and (ambient_c_min is None or float(ambient_c) >= ambient_c_min)
        and (ambient_c_max is None or float(ambient_c) <= ambient_c_max)
    )
    return bool(
        known_text(run.get("unit_id"))
        and known_text(run.get("battery_id"))
        and (
            not require_labeled_capacity
            or (
                isinstance(run.get("battery_mah"), (int, float))
                and math.isfinite(float(run["battery_mah"]))
                and float(run["battery_mah"]) > 0
                and bool(run.get("battery_photo_sha256"))
            )
        )
        and (not require_hardware_revision or known_text(run.get("hardware_revision")))
        and (not require_ambient_temperature or ambient_valid)
    )


def matrix_test_matches(run: dict, test: dict, matrix_sha256: str | None) -> bool:
    """Require a child artifact to identify and reproduce the exact matrix test."""
    if (
        not matrix_sha256
        or run.get("qualification_matrix_sha256") != matrix_sha256
        or run.get("qualification_test_id") != test.get("id")
    ):
        return False
    requested = run.get("requested_duration_min")
    if not isinstance(requested, (int, float)) or not math.isclose(
        float(requested), float(test.get("duration_min", -1)), rel_tol=0, abs_tol=1e-6
    ):
        return False
    for key in (
        "capture_ms",
        "turn_interval_s",
        "journal_gap_s",
        "turn_timeout_s",
        "say_rate",
        "say_volume",
        "ble_probe_interval_s",
        "ble_probe_timeout_s",
        "ble_config_write_interval_s",
        "boot_timeout_s",
    ):
        if key not in test:
            continue
        actual = run.get(key)
        expected = test[key]
        if not isinstance(actual, (int, float)) or not math.isclose(
            float(actual), float(expected), rel_tol=0, abs_tol=1e-6
        ):
            return False
    return True


def largest_release_cohort(
    runs: list[dict],
    require_uniform_capacity: bool = False,
    maximum_ambient_spread_c: float | None = None,
) -> list[dict]:
    """Keep the largest comparable build/cell/environment cohort."""
    cohorts: dict[tuple, list[dict]] = {}
    for run in runs:
        key = release_build_key(run)
        if key is not None:
            cohort_key: tuple = key
            if require_uniform_capacity:
                capacity = run.get("battery_mah")
                if not isinstance(capacity, (int, float)) or not math.isfinite(float(capacity)):
                    continue
                cohort_key = (*key, float(capacity))
            cohorts.setdefault(cohort_key, []).append(run)
    if not cohorts:
        return []
    comparable: list[tuple[tuple, list[dict]]] = []
    for key, cohort in cohorts.items():
        candidate = cohort
        if maximum_ambient_spread_c is not None:
            ordered = sorted(cohort, key=lambda run: float(run["ambient_c"]))
            windows = [
                [
                    run for run in ordered
                    if float(start["ambient_c"]) <= float(run["ambient_c"])
                    <= float(start["ambient_c"]) + maximum_ambient_spread_c
                ]
                for start in ordered
            ]
            candidate = max(
                windows,
                key=lambda window: (
                    len({run.get("unit_id") for run in window}),
                    len({run.get("battery_id") for run in window}),
                    len(window),
                ),
                default=[],
            )
        comparable.append((key, candidate))
    ranked = sorted(
        comparable,
        key=lambda item: (
            -len({run.get("unit_id") for run in item[1] if run.get("unit_id") != "unknown"}),
            -len({run.get("battery_id") for run in item[1] if known_text(run.get("battery_id"))}),
            item[0],
        ),
    )
    return ranked[0][1]


def claim_test_conditions(runs: list[dict]) -> str:
    capacities = sorted({float(run["battery_mah"]) for run in runs})
    temperatures = [float(run["ambient_c"]) for run in runs]
    capacity = (
        f"{capacities[0]:g} mAh"
        if len(capacities) == 1
        else f"{capacities[0]:g}–{capacities[-1]:g} mAh"
    )
    temperature = (
        f"{temperatures[0]:g} °C"
        if min(temperatures) == max(temperatures)
        else f"{min(temperatures):g}–{max(temperatures):g} °C"
    )
    return f"Tested at {temperature} with labeled {capacity} cells."


def workload_gate_passes(run: dict, test: dict) -> bool:
    ble_probes = int(run.get("ble_probes", 0))
    terminal_ble_failures = int(run.get("terminal_ble_failures", 0))
    qualified_ble_probes = max(0, ble_probes - terminal_ble_failures)
    ble_success_ratio = (
        int(run.get("successful_ble_probes", 0)) / qualified_ble_probes
        if qualified_ble_probes > 0 else 0.0
    )
    return bool(
        int(run.get("successful_turns", 0)) >= int(test.get("minimum_successful_turns", 0))
        and float(run.get("capture_coverage_ratio", 0))
        >= float(test.get("minimum_capture_coverage_ratio", 0))
        and ble_success_ratio >= float(test.get("minimum_ble_probe_success_ratio", 0))
        and int(run.get("metered_ble_config_roundtrips", 0))
        >= int(test.get("minimum_metered_ble_config_roundtrips", 0))
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
    parser.add_argument(
        "--analyzer-csv",
        type=Path,
        action="append",
        default=[],
        help=(
            "additional battery-path CSV; per-run lunasay-*/analyzer.csv files are discovered "
            "automatically"
        ),
    )
    parser.add_argument("--shutdown-current-threshold-ma", type=float, default=0.2,
                        help="maximum battery current considered electrically off")
    parser.add_argument("--shutdown-current-sustain-s", type=float, default=300.0,
                        help="required continuous near-zero tail for analyzer shutdown inference")
    parser.add_argument("--analyzer-max-gap-s", type=float, default=60.0,
                        help="largest accepted gap between direct-current samples")
    parser.add_argument("--matrix", type=Path, default=ROOT / "config" / "lunasay_power_matrix.json")
    args = parser.parse_args()
    if (
        not math.isfinite(args.shutdown_current_threshold_ma)
        or not math.isfinite(args.shutdown_current_sustain_s)
        or not math.isfinite(args.analyzer_max_gap_s)
        or args.shutdown_current_threshold_ma < 0
        or args.shutdown_current_sustain_s <= 0
        or args.analyzer_max_gap_s <= 0
    ):
        raise SystemExit(
            "error: analyzer shutdown threshold must be non-negative; sustain and maximum gap "
            "must be positive"
        )
    if args.battery_mah is not None and (
        not math.isfinite(args.battery_mah) or args.battery_mah <= 0
    ):
        raise SystemExit("error: --battery-mah must be finite and positive")
    if not math.isfinite(args.min_estimate_hours) or args.min_estimate_hours <= 0:
        raise SystemExit("error: --min-estimate-hours must be finite and positive")
    if not 0 < args.min_percent_drop <= 100:
        raise SystemExit("error: --min-percent-drop must be 1..100")
    matrix_sha256 = hashlib.sha256(args.matrix.read_bytes()).hexdigest() if args.matrix.is_file() else None
    matrix_data = load_json(args.matrix) if args.matrix.is_file() else {}
    matrix_release_gate = matrix_data.get("release_gate", {})
    minimum_start_voltage_mv = matrix_release_gate.get("minimum_start_voltage_mv", 4100)
    minimum_charge_rest_min = matrix_release_gate.get("minimum_charge_rest_min", 30)
    if (
        not isinstance(minimum_start_voltage_mv, int)
        or isinstance(minimum_start_voltage_mv, bool)
        or not 3500 <= minimum_start_voltage_mv <= 4400
        or not isinstance(minimum_charge_rest_min, (int, float))
        or isinstance(minimum_charge_rest_min, bool)
        or not math.isfinite(float(minimum_charge_rest_min))
        or minimum_charge_rest_min < 0
    ):
        raise SystemExit("error: matrix charge start/rest controls are invalid")
    minimum_charge_rest_s = float(minimum_charge_rest_min) * 60.0
    analyzer_input_paths = analyzer_paths(args.analyzer_csv, args.artifact_root)
    analyzer_rows = load_analyzer_rows(analyzer_input_paths)
    active_summary_paths = sorted(args.artifact_root.glob("lunasay-battery-*/summary.json"))
    deep_summary_paths = sorted(args.artifact_root.glob("lunasay-deep-sleep-*/summary.json"))
    consumed_artifact_paths: set[Path] = set()
    for summary_path in [*active_summary_paths, *deep_summary_paths]:
        consumed_artifact_paths.add(summary_path)
        events_path = summary_path.parent / "events.jsonl"
        if events_path.is_file():
            consumed_artifact_paths.add(events_path)
        consumed_artifact_paths.update(summary_path.parent.glob("battery-label*"))
    artifact_sources = [
        file_manifest(path)
        for path in sorted(consumed_artifact_paths, key=lambda item: str(item.resolve()))
    ]
    analyzer_sources = []
    for path in analyzer_input_paths:
        source_path = str(path.resolve())
        source_identities = sorted({
            (
                row.get("instrument_model", ""),
                row.get("instrument_serial", ""),
                row.get("calibration_ref", ""),
            )
            for row in analyzer_rows
            if row.get("source_path") == source_path
        })
        analyzer_sources.append({
            "path": str(path.resolve()),
            "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
            "bytes": path.stat().st_size,
            "instrument_identities": [
                {
                    "model": identity[0] or None,
                    "serial": identity[1] or None,
                    "calibration_ref": identity[2] or None,
                }
                for identity in source_identities
            ],
        })

    runs: list[dict] = []
    curves: list[dict] = []
    for summary_path in active_summary_paths:
        summary = load_json(summary_path)
        scenario_evidence = summary.get("scenario_evidence", {})
        analyzer_capture = summary.get("analyzer_capture")
        analyzer_capture_is_bound = analyzer_capture_bound(summary_path, analyzer_capture)
        article = summary.get("test_article", {})
        workload_config = summary.get("workload_config", {})
        charge_gate = summary.get("charge_gate", {})
        charge_status = charge_gate.get("status", {})
        charge_ready = charge_gate_passes(
            charge_gate,
            minimum_start_voltage_mv,
            minimum_charge_rest_s,
        )
        run_battery_mah = args.battery_mah
        if run_battery_mah is None and isinstance(article.get("battery_mah"), (int, float)):
            run_battery_mah = float(article["battery_mah"])
        events = load_events(summary_path.parent / "events.jsonl")
        start, end, shutdown_observed = run_window(events, summary)
        window_analyzer_rows = [
            row for row in analyzer_rows
            if row["run_id"] == summary_path.parent.name
            and start is not None and end is not None
            and start <= row["epoch_s"] <= end
        ]
        recovery = summary.get("shutdown_evidence", {})
        inferred_shutdown = analyzer_shutdown_epoch(
            window_analyzer_rows,
            args.shutdown_current_threshold_ma,
            args.shutdown_current_sustain_s,
        )
        end, shutdown_observed, shutdown_basis = reconcile_shutdown_endpoint(
            end,
            shutdown_observed,
            inferred_shutdown,
            recovery,
        )
        if (
            shutdown_basis == "analyzer-current-collapse+pmu-undervoltage-reset"
            and start is not None
            and end is not None
        ):
            scenario_evidence = reconcile_scenario_evidence(
                scenario_evidence,
                end - start,
            )
        scenario_verified = bool(scenario_evidence.get("passed", False))
        radio_off_idle_completed = bool(
            shutdown_basis == "analyzer-current-collapse+pmu-undervoltage-reset"
            and summary.get("workload") == "idle"
            and summary.get("scenario") in ("full-offline", "dim-offline", "sleep-offline")
            and summary.get("run_error") is None
            and summary.get("cleanup_error") is None
            and not summary.get("device_unreachable")
        )
        validated_summary = dict(summary)
        validated_summary["passed"] = bool(
            scenario_verified
            and (summary.get("passed", False) or radio_off_idle_completed)
        )
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
            "analyzer_instrument_model": None,
            "analyzer_instrument_serial": None,
            "analyzer_calibration_ref": None,
            "analyzer_provenance_complete": False,
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
            elif not direct_metrics.get("analyzer_provenance_complete"):
                direct_metrics["current_basis"] = "direct-battery-analyzer-unattributed"
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
        terminal_ble_failures = int(summary.get("terminal_ble_failures", 0))
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
            "hardware_revision": article.get("hardware_revision", "unknown"),
            "battery_id": article.get("battery_id", "unknown"),
            "battery_mah": run_battery_mah,
            "battery_photo_sha256": article.get("battery_photo_sha256"),
            "battery_cycle_count": article.get("battery_cycle_count"),
            "ambient_c": article.get("ambient_c"),
            "qualification_matrix_sha256": article.get("qualification_matrix_sha256"),
            "qualification_test_id": article.get("qualification_test_id"),
            "requested_duration_min": summary.get("requested_duration_min"),
            "analyzer_capture_bound": analyzer_capture_is_bound,
            "analyzer_adapter_sha256": (
                analyzer_capture.get("adapter_sha256")
                if isinstance(analyzer_capture, dict) else None
            ),
            "rested_start_percent": charge_status.get("percent"),
            "rested_start_voltage_mv": charge_status.get("mv"),
            **{
                key: workload_config.get(key)
                for key in (
                    "capture_ms", "turn_interval_s", "journal_gap_s", "turn_timeout_s",
                    "say_rate", "say_volume", "ble_probe_interval_s", "ble_probe_timeout_s",
                    "ble_config_write_interval_s",
                )
            },
            "firmware_build": article.get("firmware_build", "unknown"),
            "firmware_version": article.get("firmware_version", "unknown"),
            "firmware_variant": article.get("firmware_variant", "unknown"),
            "firmware_elf_sha256": article.get("firmware_elf_sha256"),
            "firmware_provenance_complete": bool(article.get("firmware_provenance_complete")),
            "harness_build": article.get("harness_build", "unknown"),
            "shutdown_basis": shutdown_basis,
            "turns": int(summary.get("turns", 0)),
            "accepted_captures": accepted_captures,
            "successful_turns": successful_turns,
            "ble_probes": ble_probes,
            "successful_ble_probes": successful_ble_probes,
            "terminal_ble_failures": terminal_ble_failures,
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
                "epoch_s": sample["epoch_s"],
                "percent": sample["percent"],
                "voltage_mv": sample["voltage_mv"],
            })

    args.out_dir.mkdir(parents=True, exist_ok=True)
    (args.out_dir / "analyzer_sources.json").write_text(
        json.dumps(analyzer_sources, indent=2) + "\n",
        encoding="utf-8",
    )
    (args.out_dir / "artifact_sources.json").write_text(
        json.dumps(artifact_sources, indent=2) + "\n",
        encoding="utf-8",
    )
    report_config = {
        "artifact_root": str(args.artifact_root.resolve()),
        "matrix": str(args.matrix.resolve()),
        "matrix_sha256": matrix_sha256,
        "minimum_estimate_hours": args.min_estimate_hours,
        "minimum_percent_drop": args.min_percent_drop,
        "battery_mah_override": args.battery_mah,
        "minimum_start_voltage_mv": minimum_start_voltage_mv,
        "minimum_charge_rest_s": minimum_charge_rest_s,
        "analyzer_inputs": [str(path.resolve()) for path in analyzer_input_paths],
        "analyzer_max_gap_s": args.analyzer_max_gap_s,
        "shutdown_current_threshold_ma": args.shutdown_current_threshold_ma,
        "shutdown_current_sustain_s": args.shutdown_current_sustain_s,
        "generator_path": str(Path(__file__).resolve()),
        "generator_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
    }
    (args.out_dir / "report_config.json").write_text(
        json.dumps(report_config, indent=2) + "\n",
        encoding="utf-8",
    )
    deep_runs: list[dict] = []
    for summary_path in deep_summary_paths:
        summary = load_json(summary_path)
        analyzer_capture = summary.get("analyzer_capture")
        analyzer_capture_is_bound = analyzer_capture_bound(summary_path, analyzer_capture)
        article = summary.get("test_article", {})
        charge_gate = summary.get("charge_gate", {})
        charge_status = charge_gate.get("status", {})
        charge_ready = charge_gate_passes(
            charge_gate,
            minimum_start_voltage_mv,
            minimum_charge_rest_s,
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
        start_voltage_mv = sleep.get("start_voltage_mv")
        end_percent = battery.get("percent")
        end_voltage_mv = battery.get("voltage_mv")
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
            "analyzer_instrument_model": None,
            "analyzer_instrument_serial": None,
            "analyzer_calibration_ref": None,
            "analyzer_provenance_complete": False,
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
            elif not deep_direct.get("analyzer_provenance_complete"):
                deep_direct["current_basis"] = "direct-battery-analyzer-unattributed"
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
            "hardware_revision": article.get("hardware_revision", "unknown"),
            "battery_id": article.get("battery_id", "unknown"),
            "battery_mah": run_battery_mah,
            "battery_photo_sha256": article.get("battery_photo_sha256"),
            "battery_cycle_count": article.get("battery_cycle_count"),
            "ambient_c": article.get("ambient_c"),
            "qualification_matrix_sha256": article.get("qualification_matrix_sha256"),
            "qualification_test_id": article.get("qualification_test_id"),
            "requested_duration_min": summary.get("duration_min"),
            "analyzer_capture_bound": analyzer_capture_is_bound,
            "analyzer_adapter_sha256": (
                analyzer_capture.get("adapter_sha256")
                if isinstance(analyzer_capture, dict) else None
            ),
            "boot_timeout_s": summary.get("boot_timeout_s"),
            "rested_start_percent": charge_status.get("percent"),
            "rested_start_voltage_mv": charge_status.get("mv"),
            "firmware_build": article.get("firmware_build", "unknown"),
            "firmware_version": article.get("firmware_version", "unknown"),
            "firmware_variant": article.get("firmware_variant", "unknown"),
            "firmware_elf_sha256": article.get("firmware_elf_sha256"),
            "firmware_provenance_complete": bool(article.get("firmware_provenance_complete")),
            "harness_build": article.get("harness_build", "unknown"),
            "charge_ready": charge_ready,
            "duration_h": duration_h,
            "start_percent": start_percent,
            "end_percent": end_percent,
            "start_voltage_mv": start_voltage_mv,
            "end_voltage_mv": end_voltage_mv,
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
        "scenario_counter_passed", "scenario_history_passed", "unit_id", "hardware_revision",
        "battery_id", "battery_mah", "battery_photo_sha256", "battery_cycle_count", "ambient_c",
        "qualification_matrix_sha256", "qualification_test_id", "requested_duration_min",
        "analyzer_capture_bound", "analyzer_adapter_sha256",
        "rested_start_percent", "rested_start_voltage_mv",
        "capture_ms", "turn_interval_s", "journal_gap_s", "turn_timeout_s", "say_rate", "say_volume",
        "ble_probe_interval_s", "ble_probe_timeout_s", "ble_config_write_interval_s",
        "firmware_build", "firmware_version", "firmware_variant", "firmware_elf_sha256",
        "firmware_provenance_complete", "harness_build",
        "turns", "accepted_captures", "successful_turns", "ble_probes", "successful_ble_probes",
        "terminal_ble_failures", "ble_config_roundtrips",
        "captured_audio_s", "capture_coverage_ratio", "metered_successful_turns",
        "metered_captured_audio_s", "metered_ble_config_roundtrips", "energy_per_unit_mwh",
        "duration_h", "sample_count", "start_percent", "end_percent", "start_voltage_mv",
        "end_voltage_mv", "percent_drop", "voltage_drop_mv", "percent_monotonic",
        "percent_per_hour", "voltage_drop_mv_per_hour", "projected_full_runtime_h",
        "measured_runtime_h", "median_current_ma", "average_current_ma", "peak_current_ma", "charge_mah", "energy_wh",
        "current_basis", "analyzer_samples", "analyzer_duration_h", "analyzer_coverage_ratio",
        "analyzer_median_gap_s", "analyzer_max_gap_s",
        "analyzer_instrument_model", "analyzer_instrument_serial", "analyzer_calibration_ref",
        "analyzer_provenance_complete",
        "shutdown_observed", "shutdown_basis", "charge_ready", "evidence",
    ]
    with (args.out_dir / "runs.csv").open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=run_fields)
        writer.writeheader()
        writer.writerows(runs)
    deep_fields = [
        "run_id", "scenario", "workload", "wake_source", "passed", "unit_id", "hardware_revision",
        "battery_id", "battery_mah", "battery_photo_sha256", "battery_cycle_count", "ambient_c",
        "qualification_matrix_sha256", "qualification_test_id", "requested_duration_min", "boot_timeout_s",
        "analyzer_capture_bound", "analyzer_adapter_sha256",
        "rested_start_percent", "rested_start_voltage_mv",
        "firmware_build", "firmware_version", "firmware_variant", "firmware_elf_sha256",
        "firmware_provenance_complete", "harness_build", "charge_ready",
        "duration_h", "start_percent", "end_percent", "start_voltage_mv", "end_voltage_mv",
        "percent_drop", "projected_full_runtime_h",
        "median_current_ma", "average_current_ma", "peak_current_ma", "charge_mah", "energy_wh",
        "current_basis", "analyzer_samples", "analyzer_duration_h", "analyzer_coverage_ratio",
        "analyzer_median_gap_s", "analyzer_max_gap_s",
        "analyzer_instrument_model", "analyzer_instrument_serial", "analyzer_calibration_ref",
        "analyzer_provenance_complete",
        "evidence",
    ]
    with (args.out_dir / "deep_sleep_runs.csv").open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=deep_fields)
        writer.writeheader()
        writer.writerows(deep_runs)
    curve_fields = ["run_id", "scenario", "workload", "epoch_s", "elapsed_h", "percent", "voltage_mv"]
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
        "Analyzer inputs are hashed in `analyzer_sources.json`; consumed QA artifacts are hashed "
        "in `artifact_sources.json`; all thresholds, the matrix hash, and report-generator hash "
        "are recorded in `report_config.json`; generated deliverables are sealed by "
        "`report_outputs.json`.",
        "",
        "![Battery discharge curves](curves.svg)",
        "" if args.battery_mah is None else f"Average current uses the labeled {args.battery_mah:g} mAh cell capacity.",
        "",
        "| Scenario | Workload | Unit | Duration | Samples | Battery-only start → end | Drop | Rate | Projected | Measured | Avg current | Current basis | Energy | Shutdown basis | Evidence |",
        "|---|---|---|---:|---:|---|---:|---:|---:|---:|---:|---|---:|---|---|",
    ]
    for run in runs:
        lines.append(
            f"| {run['scenario']} | {run['workload']} | {run['unit_id']} | "
            f"{fmt(run['duration_h'])} h | {run['sample_count']} | "
            f"{fmt(run['start_percent'], 0)}% / {fmt(run['start_voltage_mv'], 0)} mV → "
            f"{fmt(run['end_percent'], 0)}% / {fmt(run['end_voltage_mv'], 0)} mV | "
            f"{run['percent_drop']}% | "
            f"{fmt(run['percent_per_hour'])}%/h | {fmt(run['projected_full_runtime_h'])} h | "
            f"{fmt(run['measured_runtime_h'])} h | {fmt(run['average_current_ma'])} mA | "
            f"{run['current_basis']} | {fmt(run['energy_wh'], 3)} Wh | "
            f"{run['shutdown_basis']} | {run['evidence']} |"
        )
    if not runs:
        lines.append("| — | — | — | — | — | — | — | — | — | — | — | — | — | — | no completed runs |")
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
            "| Scenario | Workload | Instrument | Calibration | Adapter SHA | Samples | Coverage | Median gap | Max gap | Median | Average | Peak | Charge | Energy |",
            "|---|---|---|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|",
        ])
        for run in direct_runs:
            lines.append(
                f"| {run['scenario']} | {run['workload']} | "
                f"{run.get('analyzer_instrument_model') or '—'} / "
                f"{run.get('analyzer_instrument_serial') or '—'} | "
                f"{run.get('analyzer_calibration_ref') or '—'} | "
                f"{str(run.get('analyzer_adapter_sha256') or '—')[:12]} | "
                f"{run['analyzer_samples']} | "
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
            "| Workload | Scenario | Probes | Successful | Endpoint misses | Qualified success | Set roundtrips | Metered sets | Direct mWh/set interval |",
            "|---|---|---:|---:|---:|---:|---:|---:|---:|",
        ])
        for run in ble_runs:
            qualified_probes = max(
                0,
                run["ble_probes"] - run["terminal_ble_failures"],
            )
            success_ratio = (
                run["successful_ble_probes"] / qualified_probes
                if qualified_probes > 0 else 0.0
            )
            config_energy = run["energy_per_unit_mwh"] if run["workload"] == "ble-config" else None
            lines.append(
                f"| {run['workload']} | {run['scenario']} | {run['ble_probes']} | "
                f"{run['successful_ble_probes']} | {run['terminal_ble_failures']} | "
                f"{fmt(success_ratio * 100.0, 1)}% | "
                f"{run['ble_config_roundtrips']} | {run['metered_ble_config_roundtrips']} | "
                f"{fmt(config_energy, 2)} |"
            )
    if deep_runs:
        lines.extend([
            "",
            "## True deep-sleep evidence",
            "",
            "| Unit | Wake | Duration | Start → end | Drop | Projected runtime | Avg current | Current basis | Evidence |",
            "|---|---|---:|---|---:|---:|---:|---|---|",
        ])
        for run in deep_runs:
            lines.append(
                f"| {run['unit_id']} | {run['wake_source']} | {fmt(run['duration_h'])} h | "
                f"{fmt(run['start_percent'], 0)}% / {fmt(run['start_voltage_mv'], 0)} mV → "
                f"{fmt(run['end_percent'], 0)}% / {fmt(run['end_voltage_mv'], 0)} mV | "
                f"{fmt(run['percent_drop'])}% | {fmt(run['projected_full_runtime_h'])} h | "
                f"{fmt(run['average_current_ma'])} mA | {run['current_basis']} | {run['evidence']} |"
            )

    claim_rows: list[dict] = []
    matrix_open_count = 0
    if args.matrix.exists():
        matrix = matrix_data
        rank = {"failed": 0, "insufficient-samples": 1, "unqualified-runtime": 2,
                "functional-only": 2,
                "runtime-estimate": 3, "measured-runtime": 4}
        lines.extend([
            "",
            "## Required-matrix coverage",
            "",
            "| Test | Display | Radio | Workload | Best evidence | Required basis | Units | Cells | Release gate |",
            "|---|---|---|---|---|---|---:|---:|---|",
        ])
        release_gate = matrix.get("release_gate", {})
        require_direct_current = bool(release_gate.get("require_direct_current", False))
        require_analyzer_provenance = bool(
            release_gate.get("require_analyzer_provenance", False)
        )
        require_analyzer_adapter_capture = bool(
            release_gate.get("require_analyzer_adapter_capture", False)
        )
        require_labeled_capacity = bool(release_gate.get("require_labeled_capacity", False))
        require_hardware_revision = bool(release_gate.get("require_hardware_revision", False))
        require_ambient_temperature = bool(
            release_gate.get("require_ambient_temperature", False)
        )
        require_uniform_capacity = bool(
            release_gate.get("require_uniform_battery_capacity", False)
        )
        ambient_c_min = release_gate.get("ambient_c_min")
        ambient_c_max = release_gate.get("ambient_c_max")
        maximum_ambient_spread_c = release_gate.get("maximum_cohort_ambient_spread_c")
        for name, value in (
            ("ambient_c_min", ambient_c_min),
            ("ambient_c_max", ambient_c_max),
            ("maximum_cohort_ambient_spread_c", maximum_ambient_spread_c),
        ):
            if value is not None and (
                not isinstance(value, (int, float))
                or isinstance(value, bool)
                or not math.isfinite(float(value))
            ):
                raise SystemExit(f"error: matrix release_gate.{name} must be finite")
        ambient_c_min = float(ambient_c_min) if ambient_c_min is not None else None
        ambient_c_max = float(ambient_c_max) if ambient_c_max is not None else None
        maximum_ambient_spread_c = (
            float(maximum_ambient_spread_c)
            if maximum_ambient_spread_c is not None else None
        )
        if (
            ambient_c_min is not None
            and ambient_c_max is not None
            and ambient_c_min > ambient_c_max
        ) or (maximum_ambient_spread_c is not None and maximum_ambient_spread_c < 0):
            raise SystemExit("error: matrix ambient bounds/spread are invalid")
        matrix_tests = matrix.get("tests", [])
        gpio_gate_tests = [
            matrix_test for matrix_test in matrix_tests
            if matrix_test.get("runner") == "deep-sleep"
            and matrix_test.get("wake_source") == "gpio0"
            and matrix_test.get("release_basis") == "functional-gate"
        ]
        for test in matrix_tests:
            if test.get("runner") == "deep-sleep":
                candidates = [
                    run for run in deep_runs
                    if matrix_test_matches(run, test, matrix_sha256)
                ]
            else:
                candidates = [
                    run for run in runs
                    if run["scenario"] == test.get("scenario")
                    and run["workload"] == test.get("workload")
                    and matrix_test_matches(run, test, matrix_sha256)
                ]
            best = max(candidates, key=lambda run: rank.get(run["evidence"], -1)) if candidates else None
            best_evidence = best["evidence"] if best is not None else "missing"
            release_basis = test.get("release_basis", "measured-runtime")
            if release_basis == "direct-projection":
                qualifying = [
                    run for run in candidates
                    if run.get("passed")
                    and run.get("charge_ready")
                    and run.get("wake_source") == "timer"
                    and run.get("current_basis") == "direct-battery-analyzer"
                    and (
                        not require_analyzer_adapter_capture
                        or run.get("analyzer_capture_bound")
                    )
                    and isinstance(run.get("projected_full_runtime_h"), (int, float))
                    and test_article_gate_passes(
                        run,
                        require_labeled_capacity,
                        require_hardware_revision,
                        require_ambient_temperature,
                        ambient_c_min,
                        ambient_c_max,
                    )
                ]
            elif release_basis == "functional-gate":
                qualifying = [
                    run for run in candidates
                    if run.get("passed")
                    and run.get("charge_ready")
                    and run.get("wake_source") == test.get("wake_source")
                    and test_article_gate_passes(
                        run,
                        require_labeled_capacity,
                        require_hardware_revision,
                        require_ambient_temperature,
                        ambient_c_min,
                        ambient_c_max,
                    )
                ]
            elif release_basis == "direct-workload":
                qualifying = [
                    run for run in candidates
                    if run.get("passed")
                    and run.get("charge_ready")
                    and workload_gate_passes(run, test)
                    and run.get("current_basis") == "direct-battery-analyzer"
                    and (
                        not require_analyzer_adapter_capture
                        or run.get("analyzer_capture_bound")
                    )
                    and float(run.get("analyzer_coverage_ratio", 0)) >= 0.95
                    and int(run.get("successful_ble_probes", 0)) > 0
                    and int(run.get("ble_config_roundtrips", 0)) > 0
                    and test_article_gate_passes(
                        run,
                        require_labeled_capacity,
                        require_hardware_revision,
                        require_ambient_temperature,
                        ambient_c_min,
                        ambient_c_max,
                    )
                ]
            else:
                qualifying = [
                    run for run in candidates
                    if run.get("evidence") == "measured-runtime"
                    and workload_gate_passes(run, test)
                    and (
                        not require_analyzer_adapter_capture
                        or run.get("analyzer_capture_bound")
                    )
                    and (
                        not require_direct_current
                        or (
                            run.get("current_basis") == "direct-battery-analyzer"
                            and float(run.get("analyzer_coverage_ratio", 0)) >= 0.95
                        )
                    )
                    and test_article_gate_passes(
                        run,
                        require_labeled_capacity,
                        require_hardware_revision,
                        require_ambient_temperature,
                        ambient_c_min,
                        ambient_c_max,
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
                        and any(
                            matrix_test_matches(gate_run, gate_test, matrix_sha256)
                            for gate_test in gpio_gate_tests
                        )
                        for gate_run in deep_runs
                    )
                ]
            qualifying = largest_release_cohort(
                qualifying,
                require_uniform_capacity,
                maximum_ambient_spread_c,
            )
            qualifying_units = {
                run.get("unit_id", "unknown") for run in qualifying
                if run.get("unit_id") != "unknown"
            }
            qualifying_batteries = {
                run.get("battery_id", "unknown") for run in qualifying
                if known_text(run.get("battery_id"))
            }
            required_units = int(release_gate.get("units_required", 2))
            required_batteries = int(release_gate.get("batteries_required", required_units))
            gate = (
                "ready"
                if len(qualifying_units) >= required_units
                and len(qualifying_batteries) >= required_batteries
                else "open"
            )
            matrix_open_count += int(gate != "ready")
            required_basis_label = (
                "measured-runtime+direct-current"
                if release_basis == "measured-runtime" and require_direct_current
                else release_basis
            )
            if require_analyzer_provenance and release_basis != "functional-gate":
                required_basis_label += "+meter-provenance"
            if require_analyzer_adapter_capture and release_basis != "functional-gate":
                required_basis_label += "+adapter-bound"
            if require_uniform_capacity:
                required_basis_label += "+uniform-capacity"
            if require_ambient_temperature:
                if ambient_c_min is not None and ambient_c_max is not None:
                    required_basis_label += f"+{ambient_c_min:g}–{ambient_c_max:g}°C"
                if maximum_ambient_spread_c is not None:
                    required_basis_label += f"+≤{maximum_ambient_spread_c:g}°C-spread"
            if int(test.get("minimum_successful_turns", 0)) > 0:
                required_basis_label += f"+≥{int(test['minimum_successful_turns'])}-turns"
            if float(test.get("minimum_capture_coverage_ratio", 0)) > 0:
                required_basis_label += (
                    f"+≥{float(test['minimum_capture_coverage_ratio']) * 100:g}%-capture"
                )
            if float(test.get("minimum_ble_probe_success_ratio", 0)) > 0:
                required_basis_label += (
                    f"+≥{float(test['minimum_ble_probe_success_ratio']) * 100:g}%-BLE"
                )
            if int(test.get("minimum_metered_ble_config_roundtrips", 0)) > 0:
                required_basis_label += (
                    f"+≥{int(test['minimum_metered_ble_config_roundtrips'])}-metered-sets"
                )
            lines.append(
                f"| {test['id']} | {test.get('display', '—')} | {test.get('radio', '—')} | "
                f"{test.get('workload', test.get('runner', '—'))} | {best_evidence} | "
                f"{required_basis_label} | "
                f"{len(qualifying_units)} | {len(qualifying_batteries)} | {gate} |"
            )
            if gate == "ready" and release_basis == "measured-runtime":
                conditions = claim_test_conditions(qualifying)
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
                            f"and {workload_claim} workload. {conditions}"
                        ),
                    })
            elif gate == "ready" and release_basis == "direct-projection":
                conditions = claim_test_conditions(qualifying)
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
                    "draft": f"Projected deep-sleep battery life is {duration}. {conditions}",
                })
    limitations: list[str] = []
    if not runs and not deep_runs:
        limitations.append("No completed hub-controlled runs were found.")
    if any(not run.get("charge_ready", False) for run in [*runs, *deep_runs]):
        limitations.append("One or more runs skipped or failed the full-charge/30-minute-rest gate.")
    if any(not run.get("scenario_evidence_passed", False) for run in runs):
        limitations.append(
            "One or more active-mode runs lack verified battery-only display/radio scenario evidence."
        )
    if not analyzer_rows:
        limitations.append("No inline battery-path analyzer trace is present; direct current, mAh, and Wh remain unknown.")
    if any(
        str(run.get("current_basis", "")).startswith("direct-battery-analyzer")
        and not run.get("analyzer_capture_bound")
        for run in [*runs, *deep_runs]
    ):
        limitations.append(
            "At least one direct trace is not bound to a successful adapter capture that brackets "
            "the VBUS-off window and is excluded from release claims."
        )
    if any(run.get("current_basis") == "direct-battery-analyzer-gapped" for run in [*runs, *deep_runs]):
        limitations.append(
            f"At least one analyzer trace exceeds the {args.analyzer_max_gap_s:g}-second maximum "
            "sample gap and is excluded from release claims."
        )
    if any(
        run.get("current_basis") == "direct-battery-analyzer-unattributed"
        for run in [*runs, *deep_runs]
    ):
        limitations.append(
            "At least one analyzer trace lacks a consistent meter model, serial number, or "
            "calibration reference and is excluded from release claims."
        )
    if any(
        run.get("battery_mah") is None or not run.get("battery_photo_sha256")
        for run in [*runs, *deep_runs]
    ):
        limitations.append(
            "At least one test article lacks a labeled cell capacity or hashed label photo; "
            "its release gate remains open."
        )
    if any(not known_text(run.get("hardware_revision")) for run in [*runs, *deep_runs]):
        limitations.append(
            "At least one test article lacks a hardware revision and is excluded from release claims."
        )
    if any(
        not known_text(run.get("unit_id")) or not known_text(run.get("battery_id"))
        for run in [*runs, *deep_runs]
    ):
        limitations.append(
            "At least one test article lacks an identified unit or battery and is excluded from "
            "release claims."
        )
    if any(not isinstance(run.get("ambient_c"), (int, float)) for run in [*runs, *deep_runs]):
        limitations.append(
            "At least one test article lacks ambient-temperature provenance and is excluded from "
            "release claims."
        )
    if any(release_build_key(run) is None for run in [*runs, *deep_runs]):
        limitations.append(
            "At least one artifact lacks a clean LunaSay firmware version/full ELF SHA, committed "
            "harness, or hardware revision and is excluded from release claims."
        )
    if any(
        run.get("qualification_matrix_sha256") != matrix_sha256
        or not known_text(run.get("qualification_test_id"))
        for run in [*runs, *deep_runs]
    ):
        limitations.append(
            "At least one artifact is not bound to this qualification matrix SHA/test ID and is "
            "excluded from required-matrix coverage."
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
        "require full-to-shutdown runs on two release-candidate unit/cell articles. Deep-sleep wording remains "
        "explicitly projected even when it is backed by labeled capacity and direct current traces.",
        "",
    ])
    (args.out_dir / "report.md").write_text("\n".join(lines), encoding="utf-8")
    output_names = (
        "report.md",
        "runs.csv",
        "deep_sleep_runs.csv",
        "curves.csv",
        "curves.svg",
        "analyzer_sources.json",
        "artifact_sources.json",
        "report_config.json",
    )
    output_manifest = {
        "schema_version": 1,
        "files": [file_manifest(args.out_dir / name) for name in output_names],
    }
    (args.out_dir / "report_outputs.json").write_text(
        json.dumps(output_manifest, indent=2) + "\n",
        encoding="utf-8",
    )
    print(f"lunasay_power_report: wrote {len(runs)} runs and {len(curves)} samples to {args.out_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
