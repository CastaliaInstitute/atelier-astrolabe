"""Parse Astrolabe USB serial logs into a short agent-friendly triage report."""
from __future__ import annotations

import json
import re
from typing import Any

# Substrings that indicate a failed or unstable boot/runtime.
_PANIC_MARKERS = (
    "Guru Meditation Error",
    "abort() was called",
    "Stack canary watchpoint triggered",
    "assert failed",
    "LoadProhibited",
    "StoreProhibited",
    "IntegerDivideByZero",
    "Unhandled debug exception",
)

_WIFI_BT_COEX = "Should enable WiFi modem sleep when both WiFi and Bluetooth"

_FACE_GESTURE_RE = re.compile(r"\[gesture\]\s+face\s*->\s*(-?\d+)", re.I)


def analyze_serial_lines(lines: list[str]) -> dict[str, Any]:
    joined = "\n".join(lines)
    reboot_count = joined.count("Rebooting")
    rom_boot_count = sum(1 for ln in lines if ln.startswith("ESP-ROM:"))

    errors: list[str] = []
    warnings: list[str] = []
    for ln in lines:
        if len(errors) < 25 and ("[E]" in ln or " E (" in ln or "ERROR" in ln):
            errors.append(ln)
        if len(warnings) < 15 and ("[W]" in ln or " W (" in ln):
            warnings.append(ln)

    faces: list[int] = []
    for ln in lines:
        m = _FACE_GESTURE_RE.search(ln)
        if m:
            faces.append(int(m.group(1)))

    alert_lines = [ln for ln in lines if "ASTROLABE_ALERT" in ln]

    review: dict[str, Any] = {
        "line_count": len(lines),
        "crash_alert": len(alert_lines) > 0,
        "crash_alert_lines": alert_lines[-5:],
        "boot_ok": any("Mynah Astrolabe ready" in ln for ln in lines),
        "reboot_loop": reboot_count >= 2 or rom_boot_count >= 3,
        "panic": any(m in joined for m in _PANIC_MARKERS),
        "wifi_bt_coex": _WIFI_BT_COEX in joined,
        "brownout": "Brownout" in joined or "brownout detector" in joined.lower(),
        "ble_mentioned": any("presence:" in ln or "BLE " in ln for ln in lines),
        "spotify_mentioned": any("spotify" in ln.lower() for ln in lines),
        "gesture_faces": faces[-8:],
        "error_lines": errors,
        "warning_lines": warnings,
    }

    issues: list[str] = []
    if review["reboot_loop"]:
        issues.append("reboot loop")
    if review["panic"]:
        issues.append("panic/abort")
    if review["wifi_bt_coex"]:
        issues.append("WiFi+BT coexistence (modem sleep)")
    if review["brownout"]:
        issues.append("brownout")
    if not review["boot_ok"] and review["line_count"] > 8:
        issues.append("no 'Mynah Astrolabe ready'")
    if review["crash_alert"]:
        issues.append("ASTROLABE_ALERT (prior crash reset)")

    review["healthy"] = not issues and review["boot_ok"]
    review["issues"] = issues
    return review


def format_review_report(
    review: dict[str, Any],
    *,
    port: str,
    log_path: str,
    tail_lines: list[str],
) -> str:
    status = "HEALTHY" if review.get("healthy") else "NEEDS ATTENTION"
    lines_out = [
        f"status={status}",
        f"port={port}",
        f"log={log_path}",
        f"lines={review.get('line_count', 0)}",
        f"boot_ok={review.get('boot_ok')}",
        f"issues={', '.join(review.get('issues') or []) or '(none)'}",
    ]
    if review.get("gesture_faces"):
        lines_out.append(f"gesture_faces={review['gesture_faces']}")
    if review.get("error_lines"):
        lines_out.append("--- errors (sample) ---")
        lines_out.extend(review["error_lines"][:12])
    if review.get("warning_lines") and not review.get("healthy"):
        lines_out.append("--- warnings (sample) ---")
        lines_out.extend(review["warning_lines"][:6])
    lines_out.append("--- tail ---")
    lines_out.extend(tail_lines[-80:])
    return "\n".join(lines_out)


def publish_latest_artifacts(monitor_dir, log_path, review: dict[str, Any]) -> tuple[str, str]:
    """Copy log + write JSON triage beside artifacts/monitor/latest.*"""
    from pathlib import Path

    monitor_dir = Path(monitor_dir)
    monitor_dir.mkdir(parents=True, exist_ok=True)
    latest_log = monitor_dir / "latest.log"
    latest_json = monitor_dir / "latest-review.json"
    src = Path(log_path)
    if src.is_file():
        latest_log.write_text(src.read_text(encoding="utf-8", errors="replace"), encoding="utf-8")
    latest_json.write_text(json.dumps(review, indent=2), encoding="utf-8")
    return str(latest_log), str(latest_json)
