#!/usr/bin/env python3
"""Audit the evidence required to launch the LunaSay Kickstarter.

The audit is intentionally strict. It checks local campaign assets, economics,
release evidence, external Kickstarter status, demand, quotes, and photography.
Missing evidence is a launch blocker rather than an implicit pass.
"""

from __future__ import annotations

import argparse
from datetime import date
import hashlib
import json
from pathlib import Path
import re
import sys
from typing import Any

import lunasay_kickstarter_economics as economics


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_EVIDENCE = ROOT / "config" / "lunasay_kickstarter_launch_evidence.json"
DEFAULT_ECONOMICS = ROOT / "config" / "lunasay_kickstarter_economics.json"

REQUIRED_DOCS = {
    "readiness": ROOT / "docs" / "lunasay-kickstarter-launch-readiness.md",
    "campaign": ROOT / "docs" / "lunasay-kickstarter-campaign-draft.md",
    "communications": ROOT / "docs" / "lunasay-kickstarter-launch-comms.md",
    "demo_protocol": ROOT / "docs" / "lunasay-campaign-demo-validation.md",
}

REQUIRED_CAMPAIGN_SECTIONS = (
    "## Hero",
    "## What LunaSay does",
    "## Intentional voice",
    "## What works locally and what needs Wi-Fi",
    "## The physical object",
    "## Rewards",
    "## Development and manufacturing plan",
    "## Development timeline",
    "## Privacy and family data",
    "## Risks and challenges",
    "## Frequently asked questions",
    "## Creator and team",
)


def read_json(path: Path) -> dict[str, Any]:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError as exc:
        raise ValueError(f"missing file: {path}") from exc
    except json.JSONDecodeError as exc:
        raise ValueError(f"invalid JSON in {path}: {exc}") from exc
    if not isinstance(data, dict):
        raise ValueError(f"JSON root must be an object: {path}")
    return data


def resolve_evidence_path(value: Any) -> Path | None:
    if not isinstance(value, str) or not value.strip():
        return None
    path = Path(value)
    return path if path.is_absolute() else ROOT / path


def add_check(
    checks: list[dict[str, Any]],
    check_id: str,
    passed: bool,
    detail: str,
    blocker: bool = True,
) -> None:
    checks.append(
        {"id": check_id, "passed": passed, "blocker": blocker and not passed, "detail": detail}
    )


def require_paths(
    checks: list[dict[str, Any]], prefix: str, values: dict[str, Any]
) -> None:
    for name, value in values.items():
        path = resolve_evidence_path(value)
        add_check(
            checks,
            f"{prefix}.{name}",
            path is not None and path.exists(),
            f"{name}: {path if path else 'not supplied'}",
        )


def count_required_markers(path: Path) -> int:
    return path.read_text(encoding="utf-8").count("REQUIRED:")


def x_post_lengths(path: Path) -> dict[str, int]:
    text = path.read_text(encoding="utf-8")
    lengths: dict[str, int] = {}
    for heading in ("### Prelaunch announcement — X", "### Launch post — X"):
        if heading not in text:
            lengths[heading] = 10_000
            continue
        body = text.split(heading, 1)[1].split("\n### ", 1)[0].strip()
        lengths[heading] = len(body)
    return lengths


def qa_report_ready(value: Any) -> tuple[bool, str]:
    path = resolve_evidence_path(value)
    if path is None or not path.exists():
        return False, "campaign QA report not supplied"
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        return False, f"cannot read campaign QA report {path}: {exc}"
    summary = data.get("summary") if isinstance(data, dict) else None
    ready = isinstance(summary, dict) and summary.get("ready_to_film") is True
    return ready, f"campaign QA ready_to_film={summary.get('ready_to_film') if isinstance(summary, dict) else None}: {path}"


def passing_report(value: Any, label: str) -> tuple[bool, str]:
    path = resolve_evidence_path(value)
    if path is None or not path.exists():
        return False, f"{label} not supplied"
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        return False, f"cannot read {label} {path}: {exc}"
    passed = isinstance(data, dict) and data.get("passed") is True
    return passed, f"{label} passed={data.get('passed') if isinstance(data, dict) else None}: {path}"


def audit(evidence: dict[str, Any], economics_path: Path) -> dict[str, Any]:
    checks: list[dict[str, Any]] = []

    for name, path in REQUIRED_DOCS.items():
        add_check(checks, f"document.{name}", path.exists(), str(path))

    if REQUIRED_DOCS["campaign"].exists():
        campaign_text = REQUIRED_DOCS["campaign"].read_text(encoding="utf-8")
        for section in REQUIRED_CAMPAIGN_SECTIONS:
            add_check(
                checks,
                f"campaign_section.{re.sub(r'[^a-z]+', '_', section.lower()).strip('_')}",
                section in campaign_text,
                section,
            )
        campaign_markers = count_required_markers(REQUIRED_DOCS["campaign"])
        add_check(
            checks,
            "campaign.no_required_markers",
            campaign_markers == 0,
            f"{campaign_markers} REQUIRED markers remain",
        )

    if REQUIRED_DOCS["communications"].exists():
        comms_markers = count_required_markers(REQUIRED_DOCS["communications"])
        add_check(
            checks,
            "communications.no_required_markers",
            comms_markers == 0,
            f"{comms_markers} REQUIRED markers remain",
        )
        for heading, length in x_post_lengths(REQUIRED_DOCS["communications"]).items():
            add_check(
                checks,
                f"communications.x_length.{re.sub(r'[^a-z]+', '_', heading.lower()).strip('_')}",
                length <= 280,
                f"{heading}: {length}/280 characters",
            )

    try:
        economics_report = economics.calculate(economics.load_model(economics_path))
        add_check(
            checks,
            "economics.complete",
            economics_report["launch_ready"],
            f"{len(economics_report['missing_required_costs'])} required cost fields missing",
        )
    except ValueError as exc:
        economics_report = {"launch_ready": False, "error": str(exc)}
        add_check(checks, "economics.valid", False, str(exc))

    kickstarter = evidence.get("kickstarter", {})
    if not isinstance(kickstarter, dict):
        kickstarter = {}
    add_check(
        checks,
        "kickstarter.approved",
        kickstarter.get("review_status") == "approved",
        f"review_status={kickstarter.get('review_status')!r}",
    )
    for field in ("project_preview_url", "prelaunch_url"):
        value = kickstarter.get(field)
        add_check(
            checks,
            f"kickstarter.{field}",
            isinstance(value, str) and value.startswith("https://"),
            f"{field}={value!r}",
        )
    goal = kickstarter.get("approved_funding_goal")
    add_check(
        checks,
        "kickstarter.funding_goal",
        isinstance(goal, (int, float)) and not isinstance(goal, bool) and goal > 0,
        f"approved_funding_goal={goal!r}",
    )
    duration = kickstarter.get("campaign_duration_days")
    add_check(
        checks,
        "kickstarter.duration",
        isinstance(duration, int) and not isinstance(duration, bool) and duration > 0,
        f"campaign_duration_days={duration!r}",
    )

    demand = evidence.get("demand", {})
    if not isinstance(demand, dict):
        demand = {}
    commitments = demand.get("price_qualified_day_one_commitments", 0)
    coverage_rate = demand.get("minimum_goal_coverage_rate", 0.30)
    pledge = economics_report.get("weighted_average_hardware_pledge", 0)
    demand_value = commitments * pledge if isinstance(commitments, int) and commitments >= 0 else 0
    required_value = goal * coverage_rate if isinstance(goal, (int, float)) else None
    demand_passed = required_value is not None and demand_value >= required_value
    add_check(
        checks,
        "demand.goal_coverage",
        demand_passed,
        f"commitment value={demand_value:.2f}; required={required_value!r}",
    )
    demand_path = resolve_evidence_path(demand.get("evidence_path"))
    add_check(
        checks,
        "demand.evidence",
        demand_path is not None and demand_path.exists(),
        str(demand_path) if demand_path else "not supplied",
    )

    rc = evidence.get("release_candidate", {})
    if not isinstance(rc, dict):
        rc = {}
    revision = rc.get("git_revision")
    add_check(
        checks,
        "release_candidate.git_revision",
        isinstance(revision, str) and bool(re.fullmatch(r"[0-9a-f]{7,40}", revision)),
        f"git_revision={revision!r}",
    )
    firmware_hash = rc.get("firmware_sha256")
    add_check(
        checks,
        "release_candidate.firmware_sha256",
        isinstance(firmware_hash, str) and bool(re.fullmatch(r"[0-9a-f]{64}", firmware_hash)),
        f"firmware_sha256={firmware_hash!r}",
    )
    for field in ("device_id", "nvs_variant"):
        value = rc.get(field)
        add_check(
            checks,
            f"release_candidate.{field}",
            isinstance(value, str) and bool(value.strip()),
            f"{field}={value!r}",
        )

    runtime = evidence.get("device_runtime", {})
    if not isinstance(runtime, dict):
        runtime = {}
    add_check(
        checks,
        "device_runtime.lunasay_profile",
        runtime.get("profile") == "lunasay",
        f"profile={runtime.get('profile')!r}",
    )
    add_check(
        checks,
        "device_runtime.time_synced",
        runtime.get("time_synced") is True,
        f"time_synced={runtime.get('time_synced')!r}",
    )
    timezone = runtime.get("timezone")
    add_check(
        checks,
        "device_runtime.timezone_configured",
        isinstance(timezone, str) and bool(timezone.strip()) and timezone != "UTC0",
        f"timezone={timezone!r}",
    )
    add_check(
        checks,
        "device_runtime.location_valid",
        runtime.get("location_valid") is True,
        f"location_valid={runtime.get('location_valid')!r}",
    )

    physical = evidence.get("physical_qa", {})
    if not isinstance(physical, dict):
        physical = {}
    qa_ready, qa_detail = qa_report_ready(physical.get("campaign_20_run_report"))
    add_check(checks, "physical_qa.campaign_20_run_report", qa_ready, qa_detail)
    ota_ready, ota_detail = passing_report(
        physical.get("ota_recovery_report"), "local OTA/recovery report"
    )
    add_check(checks, "physical_qa.ota_recovery_report", ota_ready, ota_detail)
    require_paths(
        checks,
        "physical_qa",
        {
            k: v
            for k, v in physical.items()
            if k not in ("campaign_20_run_report", "ota_recovery_report")
        },
    )

    quotes = evidence.get("manufacturing_quotes", {})
    require_paths(checks, "manufacturing_quotes", quotes if isinstance(quotes, dict) else {})
    assets = evidence.get("campaign_assets", {})
    require_paths(checks, "campaign_assets", assets if isinstance(assets, dict) else {})

    ota_workflow = ROOT / ".github" / "workflows" / "deploy-astrolabe175c-ota.yml"
    ota_text = ota_workflow.read_text(encoding="utf-8") if ota_workflow.exists() else ""
    add_check(
        checks,
        "ota.lunasay_publish_spec",
        '"lunasay|LunaSay|astrolabe-lunasay-175"' in ota_text,
        str(ota_workflow),
    )
    ota_source = ROOT / "astrolabe175c" / "main" / "faculty175_ota.c"
    ota_source_text = ota_source.read_text(encoding="utf-8") if ota_source.exists() else ""
    add_check(
        checks,
        "ota.tls_peer_verification",
        "#define OTA_DEV_SKIP_TLS_SERVER_VERIFY 0" in ota_source_text,
        str(ota_source),
    )
    ota_release = evidence.get("ota_release", {})
    if not isinstance(ota_release, dict):
        ota_release = {}
    manifest_url = ota_release.get("production_manifest_url")
    add_check(
        checks,
        "ota.production_manifest_url",
        isinstance(manifest_url, str) and manifest_url.startswith("https://"),
        f"production_manifest_url={manifest_url!r}",
    )
    key_status = ota_release.get("public_key_status")
    add_check(
        checks,
        "ota.production_public_key",
        key_status == "production",
        f"public_key_status={key_status!r}",
    )
    signed_ready, signed_detail = passing_report(
        ota_release.get("signed_manifest_validation_report"),
        "signed manifest validation report",
    )
    add_check(checks, "ota.signed_manifest_validation", signed_ready, signed_detail)
    interrupted_ready, interrupted_detail = passing_report(
        ota_release.get("interrupted_transfer_report"),
        "interrupted transfer report",
    )
    add_check(checks, "ota.interrupted_transfer_recovery", interrupted_ready, interrupted_detail)

    target_value = evidence.get("target_launch_date")
    try:
        target = date.fromisoformat(str(target_value))
        add_check(checks, "launch_date.valid", True, target.isoformat(), blocker=False)
    except ValueError:
        target = None
        add_check(checks, "launch_date.valid", False, f"invalid target {target_value!r}")

    blockers = [check for check in checks if check["blocker"]]
    return {
        "ready_to_launch": not blockers,
        "target_launch_date": target.isoformat() if target else target_value,
        "checks_passed": sum(1 for check in checks if check["passed"]),
        "checks_total": len(checks),
        "blocker_count": len(blockers),
        "checks": checks,
        "blockers": blockers,
        "economics": economics_report,
        "evidence_sha256": hashlib.sha256(
            json.dumps(evidence, sort_keys=True).encode("utf-8")
        ).hexdigest(),
    }


def text_report(report: dict[str, Any]) -> str:
    verdict = "READY TO LAUNCH" if report["ready_to_launch"] else "NOT READY TO LAUNCH"
    lines = [
        "LunaSay Kickstarter launch audit",
        f"Verdict: {verdict}",
        f"Target: {report['target_launch_date']}",
        f"Checks: {report['checks_passed']}/{report['checks_total']} passed",
        f"Blockers: {report['blocker_count']}",
    ]
    if report["blockers"]:
        lines.append("Blocking evidence:")
        for check in report["blockers"]:
            lines.append(f"- {check['id']}: {check['detail']}")
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--evidence", type=Path, default=DEFAULT_EVIDENCE)
    parser.add_argument("--economics", type=Path, default=DEFAULT_ECONOMICS)
    parser.add_argument("--json", action="store_true")
    parser.add_argument(
        "--allow-incomplete",
        action="store_true",
        help="exit zero even when launch blockers remain",
    )
    args = parser.parse_args()
    try:
        report = audit(read_json(args.evidence), args.economics)
    except ValueError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2
    print(json.dumps(report, indent=2) if args.json else text_report(report), end="" if not args.json else "\n")
    if not report["ready_to_launch"] and not args.allow_incomplete:
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
