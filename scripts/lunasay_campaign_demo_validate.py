#!/usr/bin/env python3
"""Validate the repeatability of the LunaSay Kickstarter hero demonstration.

The harness guides an operator through repeated, real-device demo runs and
writes an auditable JSON record plus a human-readable Markdown report. Visual
and physical checks intentionally require operator confirmation; they are not
inferred from firmware logs or simulated UI.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from datetime import datetime, timezone
import json
import math
from pathlib import Path
import shutil
import statistics
import subprocess
import sys
import time
from typing import Any


ROOT = Path(__file__).resolve().parents[1]


@dataclass(frozen=True)
class Check:
    id: str
    title: str
    instruction: str


PREFLIGHT_CHECKS = (
    Check("real_prototype", "Real prototype", "Use the physical LunaSay unit that will appear on camera."),
    Check("release_configuration", "Representative build", "Confirm hardware and firmware represent the promised reward."),
    Check("test_profiles", "Consent-safe profiles", "Load fictional or explicitly consented profiles for natal and synastry views."),
    Check("real_services", "Production path", "Use real data and services; do not use a stub, mock, or simulated response."),
    Check("unedited_clock", "Continuous timing", "Keep a visible continuous recording or clock so failures and latency remain evident."),
)

CORE_CHECKS = (
    Check("wake_moon", "Wake into Moon", "Wake the device and verify the Moon face is readable and current."),
    Check("settings", "Settings", "Open the settings gear and verify battery, Wi-Fi, BLE, and device status are visible."),
    Check("natal_chart", "Natal chart", "Open the saved test profile and verify the intended natal chart renders clearly."),
    Check("live_transits", "Live transits", "Open current transits and verify one named aspect against the approved reference."),
    Check("synastry", "Synastry", "Compare the two test profiles and verify one named relationship/aspect."),
    Check("intentional_voice", "Intentional voice", "Hold to ask the approved prompt, release, and wait for the real answer."),
    Check("quiet_return", "Return to quiet", "Return to Moon or the dock with no feed, unsolicited audio, or stuck listening state."),
)

OFFLINE_CHECK = Check(
    "offline_boundary",
    "Offline boundary",
    "Disable Wi-Fi and verify only the behavior the campaign will explicitly claim; state unavailable functions honestly.",
)

VOICE_PROMPT = "What is most active in my chart tonight?"


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="seconds")


def git_value(*args: str) -> str:
    try:
        result = subprocess.run(
            ["git", *args], cwd=ROOT, check=True, capture_output=True, text=True
        )
        return result.stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return "unknown"


def ask_text(prompt: str, default: str = "") -> str:
    suffix = f" [{default}]" if default else ""
    value = input(f"{prompt}{suffix}: ").strip()
    return value or default


def ask_yes_no(prompt: str) -> bool:
    while True:
        value = input(f"{prompt} [y/n]: ").strip().lower()
        if value in {"y", "yes"}:
            return True
        if value in {"n", "no"}:
            return False
        print("Enter y or n.")


def run_check(check: Check) -> dict[str, Any]:
    print(f"\n  {check.title}: {check.instruction}")
    passed = ask_yes_no("  PASS?")
    notes = "" if passed else ask_text("  Failure notes")
    return {
        "id": check.id,
        "title": check.title,
        "passed": passed,
        "notes": notes,
        "recorded_at": utc_now(),
    }


def play_user_prompt(voice: str, rate: int) -> None:
    command = ["say"]
    if voice:
        command += ["-v", voice]
    command += ["-r", str(rate), VOICE_PROMPT]
    try:
        subprocess.run(command, check=True)
    except subprocess.CalledProcessError as exc:
        raise RuntimeError(f"macOS say failed with exit code {exc.returncode}") from exc


def run_voice_check(max_latency_s: float, say_voice: str, say_rate: int) -> dict[str, Any]:
    check = next(item for item in CORE_CHECKS if item.id == "intentional_voice")
    print(f"\n  {check.title}: {check.instruction}")
    print(f'  Approved prompt: "{VOICE_PROMPT}"')
    voice_label = say_voice or "system default"
    print(f"  Mac voice: {voice_label}; rate: {say_rate} words/minute")
    input("  Hold the device's talk control, then press Enter to play the Mac user...")
    play_user_prompt(say_voice, say_rate)
    print("  Prompt complete: release the talk control now.")
    started = time.monotonic()
    input("  Press Enter when intelligible response audio begins...")
    latency_s = round(time.monotonic() - started, 3)
    functional = ask_yes_no("  Was listening visibly intentional and the answer relevant/intelligible?")
    within_limit = latency_s <= max_latency_s
    passed = functional and within_limit
    notes = ""
    if not within_limit:
        notes = f"Voice latency {latency_s:.3f}s exceeded {max_latency_s:.3f}s gate."
    if not functional:
        detail = ask_text("  Failure notes")
        notes = "; ".join(part for part in (notes, detail) if part)
    print(f"  Measured voice latency: {latency_s:.3f}s ({'PASS' if passed else 'FAIL'})")
    return {
        "id": check.id,
        "title": check.title,
        "passed": passed,
        "functional": functional,
        "latency_s": latency_s,
        "max_latency_s": max_latency_s,
        "prompt_player": "macOS say",
        "say_voice": voice_label,
        "say_rate_wpm": say_rate,
        "notes": notes,
        "recorded_at": utc_now(),
    }


def percentile(values: list[float], fraction: float) -> float | None:
    if not values:
        return None
    ordered = sorted(values)
    index = max(0, math.ceil(fraction * len(ordered)) - 1)
    return ordered[index]


def checklist(offline_claim: bool) -> tuple[Check, ...]:
    return CORE_CHECKS + ((OFFLINE_CHECK,) if offline_claim else ())


def summarize(session: dict[str, Any]) -> dict[str, Any]:
    runs = session["runs"]
    expected = session["configuration"]["runs_requested"]
    minimum = session["configuration"]["minimum_passing_runs"]
    required_checks = checklist(session["configuration"]["offline_claim"])
    passing_runs = sum(1 for run in runs if run["passed"])
    check_totals: dict[str, dict[str, Any]] = {}
    for check in required_checks:
        results = [
            result
            for run in runs
            for result in run["checks"]
            if result["id"] == check.id
        ]
        check_totals[check.id] = {
            "title": check.title,
            "passed": sum(1 for result in results if result["passed"]),
            "attempted": len(results),
        }

    latencies = [
        float(result["latency_s"])
        for run in runs
        for result in run["checks"]
        if result["id"] == "intentional_voice" and "latency_s" in result
    ]
    preflight_passed = all(item["passed"] for item in session["preflight"])
    complete = len(runs) == expected
    campaign_protocol_met = expected >= 20 and minimum >= math.ceil(expected * 0.95)
    every_feature_meets_gate = all(
        total["passed"] >= minimum for total in check_totals.values()
    )
    ready = (
        campaign_protocol_met
        and complete
        and preflight_passed
        and passing_runs >= minimum
        and every_feature_meets_gate
    )
    return {
        "ready_to_film": ready,
        "completed_runs": len(runs),
        "expected_runs": expected,
        "passing_runs": passing_runs,
        "minimum_passing_runs": minimum,
        "campaign_protocol_met": campaign_protocol_met,
        "run_reliability_percent": round((passing_runs / len(runs) * 100), 1) if runs else 0.0,
        "preflight_passed": preflight_passed,
        "all_features_meet_gate": every_feature_meets_gate,
        "checks": check_totals,
        "voice_latency_s": {
            "samples": len(latencies),
            "median": round(statistics.median(latencies), 3) if latencies else None,
            "p95": round(percentile(latencies, 0.95), 3) if latencies else None,
            "maximum": round(max(latencies), 3) if latencies else None,
        },
    }


def markdown_report(session: dict[str, Any]) -> str:
    summary = session["summary"]
    cfg = session["configuration"]
    verdict = "READY TO FILM" if summary["ready_to_film"] else "NOT READY TO FILM"
    lines = [
        "# LunaSay campaign demo validation",
        "",
        f"**Verdict: {verdict}**",
        "",
        f"- Operator: {session['operator']}",
        f"- Device: {session['device_id']}",
        f"- Firmware/build: {session['firmware']}",
        f"- Git revision: `{session['source']['revision']}` ({session['source']['worktree']})",
        f"- Completed: {session['completed_at']}",
        f"- Full-sequence reliability: {summary['passing_runs']} / {summary['completed_runs']} "
        f"({summary['run_reliability_percent']:.1f}%)",
        f"- Required gate: {summary['minimum_passing_runs']} / {summary['expected_runs']}",
        f"- Campaign protocol met: {'yes' if summary['campaign_protocol_met'] else 'no'}",
        f"- Offline claim tested: {'yes' if cfg['offline_claim'] else 'no'}",
        f"- User-prompt audio: macOS `say`, voice {cfg['say_voice']}, {cfg['say_rate_wpm']} words/minute",
        "",
        "## Feature results",
        "",
        "| Feature | Passed | Attempted | Gate |",
        "|---|---:|---:|---:|",
    ]
    for result in summary["checks"].values():
        lines.append(
            f"| {result['title']} | {result['passed']} | {result['attempted']} | "
            f"{summary['minimum_passing_runs']} |"
        )
    latency = summary["voice_latency_s"]
    lines += ["", "## Voice latency", ""]
    if latency["samples"]:
        lines += [
            f"- Median: {latency['median']:.3f}s",
            f"- P95: {latency['p95']:.3f}s",
            f"- Maximum: {latency['maximum']:.3f}s",
            f"- Per-run maximum gate: {cfg['max_voice_latency_s']:.3f}s",
        ]
    else:
        lines.append("No samples recorded.")

    failures = [
        (run["number"], result)
        for run in session["runs"]
        for result in run["checks"]
        if not result["passed"]
    ]
    lines += ["", "## Failures", ""]
    if failures:
        for run_number, result in failures:
            detail = result.get("notes") or "No notes supplied."
            lines.append(f"- Run {run_number}, {result['title']}: {detail}")
    else:
        lines.append("None.")

    lines += ["", "## Evidence", ""]
    any_evidence = False
    for run in session["runs"]:
        if run["evidence"]:
            any_evidence = True
            lines.append(f"- Run {run['number']}: {run['evidence']}")
    if not any_evidence:
        lines.append("No evidence references supplied.")
    lines.append("")
    return "\n".join(lines)


def write_outputs(session: dict[str, Any], outdir: Path) -> None:
    outdir.mkdir(parents=True, exist_ok=True)
    (outdir / "summary.json").write_text(json.dumps(session, indent=2) + "\n", encoding="utf-8")
    (outdir / "report.md").write_text(markdown_report(session), encoding="utf-8")


def print_checklist(offline_claim: bool) -> None:
    print("PREFLIGHT")
    for check in PREFLIGHT_CHECKS:
        print(f"- {check.title}: {check.instruction}")
    print("\nEACH RUN")
    for check in checklist(offline_claim):
        print(f"- {check.title}: {check.instruction}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runs", type=int, default=20, help="complete sequences to attempt (default: 20)")
    parser.add_argument("--min-passes", type=int, default=19, help="passing sequences/features required (default: 19)")
    parser.add_argument("--max-voice-latency", type=float, default=20.0, help="maximum acceptable response-start latency in seconds")
    parser.add_argument("--say-voice", default="", help="macOS say voice (default: system voice)")
    parser.add_argument("--say-rate", type=int, default=175, help="macOS say speaking rate in words/minute (default: 175)")
    parser.add_argument("--offline-claim", action="store_true", help="include offline-boundary behavior as a required campaign claim")
    parser.add_argument("--operator", default="", help="operator name; prompted if omitted")
    parser.add_argument("--device-id", default="", help="prototype serial or asset ID; prompted if omitted")
    parser.add_argument("--firmware", default="", help="firmware build/version; prompted if omitted")
    parser.add_argument("--output-dir", type=Path, help="artifact directory (default: artifacts/qa/lunasay-demo-TIMESTAMP)")
    parser.add_argument("--print-checklist", action="store_true", help="print the procedure without starting a session")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.print_checklist:
        print_checklist(args.offline_claim)
        return 0
    if args.runs < 1 or not 1 <= args.min_passes <= args.runs:
        raise SystemExit("error: require runs >= 1 and 1 <= min-passes <= runs")
    if args.max_voice_latency <= 0:
        raise SystemExit("error: max-voice-latency must be positive")
    if args.say_rate <= 0:
        raise SystemExit("error: say-rate must be positive")
    if shutil.which("say") is None:
        raise SystemExit("error: macOS `say` command not found")

    stamp = time.strftime("%Y%m%d-%H%M%S")
    outdir = args.output_dir or ROOT / "artifacts" / "qa" / f"lunasay-demo-{stamp}"
    session: dict[str, Any] = {
        "schema_version": 1,
        "campaign": "LunaSay Kickstarter",
        "started_at": utc_now(),
        "completed_at": None,
        "operator": args.operator or ask_text("Operator"),
        "device_id": args.device_id or ask_text("Device serial/asset ID"),
        "firmware": args.firmware or ask_text("Firmware build/version", git_value("rev-parse", "--short", "HEAD")),
        "source": {
            "revision": git_value("rev-parse", "HEAD"),
            "branch": git_value("branch", "--show-current"),
            "worktree": "dirty" if git_value("status", "--porcelain") else "clean",
        },
        "configuration": {
            "runs_requested": args.runs,
            "minimum_passing_runs": args.min_passes,
            "max_voice_latency_s": args.max_voice_latency,
            "offline_claim": args.offline_claim,
            "voice_prompt": VOICE_PROMPT,
            "prompt_player": "macOS say",
            "say_voice": args.say_voice or "system default",
            "say_rate_wpm": args.say_rate,
        },
        "preflight": [],
        "runs": [],
    }

    print("\nLunaSay campaign demo validation")
    print(f"Artifacts: {outdir}")
    print("Record what the physical prototype actually does. Do not credit a simulated or edited result.")
    try:
        print("\nPREFLIGHT")
        session["preflight"] = [run_check(check) for check in PREFLIGHT_CHECKS]
        write_outputs({**session, "summary": summarize(session)}, outdir)

        required = checklist(args.offline_claim)
        for run_number in range(1, args.runs + 1):
            print(f"\n{'=' * 64}\nRUN {run_number} OF {args.runs}\n{'=' * 64}")
            input("Press Enter when the device and continuous recording are ready...")
            results = []
            for check in required:
                result = (
                    run_voice_check(args.max_voice_latency, args.say_voice, args.say_rate)
                    if check.id == "intentional_voice"
                    else run_check(check)
                )
                results.append(result)
            evidence = ask_text("  Evidence file/timecode (optional)")
            run = {
                "number": run_number,
                "passed": all(result["passed"] for result in results),
                "checks": results,
                "evidence": evidence,
                "completed_at": utc_now(),
            }
            session["runs"].append(run)
            print(f"\nRUN {run_number}: {'PASS' if run['passed'] else 'FAIL'}")
            write_outputs({**session, "summary": summarize(session)}, outdir)
    except (EOFError, KeyboardInterrupt):
        print("\nSession interrupted; partial results were preserved.", file=sys.stderr)

    session["completed_at"] = utc_now()
    session["summary"] = summarize(session)
    write_outputs(session, outdir)
    verdict = "READY TO FILM" if session["summary"]["ready_to_film"] else "NOT READY TO FILM"
    print(f"\n{verdict}")
    print(f"JSON: {outdir / 'summary.json'}")
    print(f"Report: {outdir / 'report.md'}")
    return 0 if session["summary"]["ready_to_film"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
