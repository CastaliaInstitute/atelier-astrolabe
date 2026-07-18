"""Shared host-side controls for attributable LunaSay power tests."""

from __future__ import annotations

from datetime import datetime, timezone
import json
from pathlib import Path
import re
import time
from typing import Callable


POWER_FIELD_RE = re.compile(r"\b([a-z_]+)=([^\s]+)")


def parse_power_status(output: str) -> dict:
    lines = [line for line in output.splitlines() if line.startswith("power: ") and "percent=" in line]
    if not lines:
        raise RuntimeError(f"power status line missing from serial output: {output[-500:]}")
    fields = dict(POWER_FIELD_RE.findall(lines[-1]))
    for key in ("percent", "mv"):
        if key in fields:
            fields[key] = int(fields[key])
    return fields


def wait_for_charge_ready(
    sample: Callable[[], str],
    log_path: Path,
    rest_min: float,
    timeout_min: float,
) -> dict:
    """Wait for charge termination and a continuous docked rest interval."""
    if rest_min < 0 or timeout_min <= 0:
        raise ValueError("invalid charge gate timing")
    started = time.monotonic()
    deadline = started + timeout_min * 60.0
    ready_since: float | None = None
    last: dict = {}
    while time.monotonic() < deadline:
        output = sample()
        status = parse_power_status(output)
        last = status
        charge_terminated = bool(
            status.get("docked") == "yes"
            and status.get("vbus") == "yes"
            and status.get("charging") == "no"
            and int(status.get("percent", -1)) >= 99
        )
        now = time.monotonic()
        if charge_terminated:
            if ready_since is None:
                ready_since = now
        else:
            ready_since = None
        rested_s = now - ready_since if ready_since is not None else 0.0
        row = {
            "at": datetime.now(timezone.utc).isoformat(),
            "elapsed_s": round(now - started, 3),
            "charge_terminated": charge_terminated,
            "rested_s": round(rested_s, 3),
            "required_rest_s": rest_min * 60.0,
            "status": status,
        }
        with log_path.open("a", encoding="utf-8") as handle:
            handle.write(json.dumps(row) + "\n")
        if charge_terminated and rested_s >= rest_min * 60.0:
            return row
        time.sleep(min(60.0, max(1.0, deadline - time.monotonic())))
    raise TimeoutError(f"battery did not reach charge-ready state; last={last}")
