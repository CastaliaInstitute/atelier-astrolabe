#!/usr/bin/env python3
"""Validate LunaSay timed deep sleep on battery through a switched USB hub."""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
import glob
import json
from pathlib import Path
import shutil
import subprocess
import time
from urllib import request

import serial

from lunasay_power_common import wait_for_charge_ready


ROOT = Path(__file__).resolve().parents[1]


def now_iso() -> str:
    return datetime.now(timezone.utc).isoformat()


def resolve_port(value: str) -> str:
    if value:
        return value
    ports = sorted(glob.glob("/dev/cu.usbmodem*") + glob.glob("/dev/ttyACM*"))
    if len(ports) != 1:
        raise RuntimeError(f"expected one USB serial device, found {ports}")
    return ports[0]


def serial_commands(port: str, commands: list[str], settle_s: float = 2.0) -> str:
    with serial.Serial(port, 115200, timeout=0.2) as ser:
        ser.reset_input_buffer()
        ser.write(("\n".join(commands) + "\n").encode())
        ser.flush()
        time.sleep(settle_s)
        return ser.read_all().decode("utf-8", "replace")


def set_hub_power(tool: str, location: str, port: int, enabled: bool) -> None:
    subprocess.run(
        [tool, "-l", location, "-p", str(port), "-a", "on" if enabled else "off"],
        check=True,
        stdout=subprocess.DEVNULL,
    )


def ping(ip: str) -> bool:
    return subprocess.run(
        ["ping", "-c", "1", "-W", "1000", ip],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    ).returncode == 0


def json_get(url: str, timeout_s: float = 5.0) -> dict:
    with request.urlopen(url, timeout=timeout_s) as response:
        return json.loads(response.read())


def wait_for_port(timeout_s: float) -> str:
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        ports = sorted(glob.glob("/dev/cu.usbmodem*") + glob.glob("/dev/ttyACM*"))
        if len(ports) == 1:
            return ports[0]
        time.sleep(0.5)
    raise RuntimeError("USB serial did not return after VBUS restore")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--duration-min", type=int, default=2)
    parser.add_argument("--ip", default="192.168.86.72")
    parser.add_argument("--port", default="")
    parser.add_argument("--hub-location", default="0-1.3")
    parser.add_argument("--hub-port", type=int, default=1)
    parser.add_argument("--uhubctl", default="/opt/homebrew/bin/uhubctl")
    parser.add_argument("--boot-timeout-s", type=float, default=120.0)
    parser.add_argument("--out-dir", default="")
    parser.add_argument("--unit-id", default="dev-unit-1")
    parser.add_argument("--battery-id", default="unlabeled")
    parser.add_argument("--battery-mah", type=float, default=None)
    parser.add_argument("--ambient-c", type=float, default=None)
    parser.add_argument("--rest-min", type=float, default=30.0)
    parser.add_argument("--charge-ready-timeout-min", type=float, default=360.0)
    parser.add_argument("--allow-not-ready", action="store_true",
                        help="skip full-charge/rest gate; smoke tests only")
    args = parser.parse_args()
    if not 1 <= args.duration_min <= 10080:
        raise SystemExit("error: --duration-min must be 1..10080")
    if not shutil.which(args.uhubctl):
        raise SystemExit(f"error: uhubctl not found: {args.uhubctl}")

    port = resolve_port(args.port)
    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    out_dir = Path(args.out_dir) if args.out_dir else ROOT / "artifacts" / "qa" / f"lunasay-deep-sleep-{stamp}"
    out_dir.mkdir(parents=True, exist_ok=True)
    events_path = out_dir / "events.jsonl"
    charge_gate: dict = {"skipped": args.allow_not_ready}
    if not args.allow_not_ready:
        charge_gate = wait_for_charge_ready(
            lambda: serial_commands(port, ["power"], settle_s=2.0),
            out_dir / "charge-ready.jsonl",
            args.rest_min,
            args.charge_ready_timeout_min,
        )
    started = time.monotonic()

    def record(kind: str, **fields: object) -> None:
        row = {"at": now_iso(), "elapsed_s": round(time.monotonic() - started, 3), "kind": kind, **fields}
        with events_path.open("a", encoding="utf-8") as handle:
            handle.write(json.dumps(row) + "\n")
        print(json.dumps(row), flush=True)

    preflight = serial_commands(
        port,
        [
            "faces profile lunasay",
            "power deep-sleep cancel",
            "power",
            f"power deep-sleep {args.duration_min}",
            "power deep-sleep status",
        ],
        3.0,
    )
    (out_dir / "preflight-serial.log").write_text(preflight, encoding="utf-8")
    if "deep-sleep pending=yes" not in preflight:
        raise RuntimeError("firmware did not arm deep sleep")
    record("armed", duration_min=args.duration_min)

    power_off = False
    became_unreachable = False
    woke_on_network = False
    wake_battery: dict = {}
    try:
        set_hub_power(args.uhubctl, args.hub_location, args.hub_port, False)
        power_off = True
        off_at = time.monotonic()
        record("vbus_off")

        offline_deadline = off_at + 45.0
        while time.monotonic() < offline_deadline:
            if not ping(args.ip):
                became_unreachable = True
                record("sleep_observed", after_vbus_off_s=round(time.monotonic() - off_at, 3))
                break
            time.sleep(1.0)
        if not became_unreachable:
            record("sleep_not_observed")

        earliest_timer_wake = off_at + args.duration_min * 60.0
        if time.monotonic() < earliest_timer_wake:
            time.sleep(earliest_timer_wake - time.monotonic())
        wake_deadline = earliest_timer_wake + args.boot_timeout_s
        while time.monotonic() < wake_deadline:
            if ping(args.ip):
                woke_on_network = True
                record("network_wake", after_vbus_off_s=round(time.monotonic() - off_at, 3))
                try:
                    wake_battery = json_get(f"http://{args.ip}/api/battery")
                    record("wake_battery", sample=wake_battery)
                except Exception as exc:
                    record("wake_battery_unavailable", error=str(exc))
                break
            time.sleep(2.0)
    finally:
        if power_off:
            set_hub_power(args.uhubctl, args.hub_location, args.hub_port, True)
            record("vbus_on")
            time.sleep(3.0)

    port = wait_for_port(20.0)
    postflight = serial_commands(port, ["power deep-sleep status", "power"], 3.0)
    (out_dir / "postflight-serial.log").write_text(postflight, encoding="utf-8")
    retained = "retained=yes" in postflight
    completed = "completed=yes" in postflight
    passed = became_unreachable and retained and completed
    summary = {
        "passed": passed,
        "duration_min": args.duration_min,
        "became_unreachable": became_unreachable,
        "woke_on_network": woke_on_network,
        "retained": retained,
        "completed": completed,
        "wake_battery": wake_battery,
        "test_article": {
            "unit_id": args.unit_id,
            "battery_id": args.battery_id,
            "battery_mah": args.battery_mah,
            "ambient_c": args.ambient_c,
            "firmware_build": subprocess.check_output(
                ["git", "describe", "--always", "--dirty"], cwd=ROOT, text=True
            ).strip(),
        },
        "charge_gate": charge_gate,
        "elapsed_s": round(time.monotonic() - started, 3),
        "preflight_serial": preflight,
        "postflight_serial": postflight,
    }
    (out_dir / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(f"lunasay_deep_sleep_validate: {'PASS' if passed else 'FAIL'} {out_dir}")
    return 0 if passed else 2


if __name__ == "__main__":
    raise SystemExit(main())
