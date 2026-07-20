#!/usr/bin/env python3
"""Restore a LunaSay test hub port after the guarded process exits.

This process is intentionally independent from the battery runner. A normal
runner restores VBUS in its own cleanup path; this guard covers abrupt process
death where Python finally blocks cannot run.
"""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
import math
import os
from pathlib import Path
import subprocess
import time


def append_log(path: Path, message: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    at = datetime.now(timezone.utc).isoformat()
    with path.open("a", encoding="utf-8") as handle:
        handle.write(f"{at} {message}\n")
        handle.flush()
        os.fsync(handle.fileno())


def process_exists(pid: int) -> bool:
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        return True
    return True


def process_identity(pid: int) -> tuple[str, str] | None:
    """Return a stable-enough process identity and detect rapid PID reuse."""
    try:
        result = subprocess.run(
            ["ps", "-p", str(pid), "-o", "lstart=", "-o", "command="],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            check=False,
        )
    except OSError:
        return ("pid", str(pid)) if process_exists(pid) else None
    started = result.stdout.strip()
    return ("ps", started) if result.returncode == 0 and started else None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pid", type=int, required=True)
    parser.add_argument("--uhubctl", required=True)
    parser.add_argument("--hub-location", required=True)
    parser.add_argument("--hub-port", type=int, required=True)
    parser.add_argument("--log", type=Path, required=True)
    parser.add_argument("--poll-s", type=float, default=1.0)
    args = parser.parse_args()

    if args.pid <= 1:
        raise SystemExit("error: --pid must identify a non-system process")
    if args.hub_port <= 0:
        raise SystemExit("error: --hub-port must be positive")
    if not math.isfinite(args.poll_s) or not 0.05 <= args.poll_s <= 60:
        raise SystemExit("error: --poll-s must be finite and between 0.05 and 60")

    identity = process_identity(args.pid)
    append_log(args.log, f"armed pid={args.pid} identity={identity!r}")
    while identity is not None and process_identity(args.pid) == identity:
        time.sleep(args.poll_s)

    append_log(args.log, "guarded process exited; forcing VBUS on")
    try:
        result = subprocess.run(
            [
                args.uhubctl,
                "-l", args.hub_location,
                "-p", str(args.hub_port),
                "-a", "on",
            ],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=False,
        )
    except OSError as exc:
        append_log(args.log, f"restore launch failed: {type(exc).__name__}: {exc}")
        return 127
    output = result.stdout.strip().replace("\n", " | ")
    append_log(args.log, f"restore returncode={result.returncode} output={output!r}")
    return result.returncode


if __name__ == "__main__":
    raise SystemExit(main())
