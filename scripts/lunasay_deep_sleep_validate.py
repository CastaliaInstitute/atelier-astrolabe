#!/usr/bin/env python3
"""Validate LunaSay timer or BOOT/GPIO0 deep-sleep wake on switched battery power."""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
import glob
import hashlib
import json
import math
from pathlib import Path
import re
import shutil
import subprocess
import time
from urllib import request

import serial

from lunasay_power_common import firmware_provenance, parse_power_status, wait_for_charge_ready


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


def wait_json_get(url: str, timeout_s: float = 30.0) -> dict:
    """Retry through the thin listener's intentional first-request wake."""
    deadline = time.monotonic() + timeout_s
    last_error: Exception | None = None
    while time.monotonic() < deadline:
        try:
            return json_get(url)
        except Exception as exc:
            last_error = exc
            time.sleep(1.0)
    raise RuntimeError(f"battery API did not return after wake: {last_error}")


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
    parser.add_argument("--wake-source", choices=("timer", "gpio0"), default="timer")
    parser.add_argument("--ip", default="192.168.86.72")
    parser.add_argument("--port", default="")
    parser.add_argument("--hub-location", default="0-1.3")
    parser.add_argument("--hub-port", type=int, default=1)
    parser.add_argument("--uhubctl", default="/opt/homebrew/bin/uhubctl")
    parser.add_argument("--boot-timeout-s", type=float, default=120.0)
    parser.add_argument("--out-dir", default="")
    parser.add_argument("--unit-id", default="dev-unit-1")
    parser.add_argument("--hardware-revision", default="")
    parser.add_argument("--battery-id", default="unlabeled")
    parser.add_argument("--battery-mah", type=float, default=None)
    parser.add_argument("--battery-photo", type=Path, default=None)
    parser.add_argument("--battery-cycle-count", type=int, default=None)
    parser.add_argument("--ambient-c", type=float, default=None)
    parser.add_argument("--qualification-matrix-sha256", default="")
    parser.add_argument("--qualification-test-id", default="")
    parser.add_argument("--rest-min", type=float, default=30.0)
    parser.add_argument("--charge-ready-timeout-min", type=float, default=360.0)
    parser.add_argument("--allow-not-ready", action="store_true",
                        help="skip full-charge/rest gate; smoke tests only")
    args = parser.parse_args()
    harness_build = subprocess.check_output(
        ["git", "describe", "--always", "--dirty"], cwd=ROOT, text=True
    ).strip()
    if not 1 <= args.duration_min <= 10080:
        raise SystemExit("error: --duration-min must be 1..10080")
    if args.battery_mah is not None and (
        not math.isfinite(args.battery_mah) or args.battery_mah <= 0
    ):
        raise SystemExit("error: --battery-mah must be finite and positive")
    if args.ambient_c is not None and not math.isfinite(args.ambient_c):
        raise SystemExit("error: --ambient-c must be finite")
    if not math.isfinite(args.boot_timeout_s) or args.boot_timeout_s <= 0:
        raise SystemExit("error: --boot-timeout-s must be finite and positive")
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
            "error: qualified runs require --battery-mah, --battery-photo, "
            "--hardware-revision, --ambient-c, and identified --unit-id/--battery-id"
        )
    if args.wake_source == "gpio0" and args.duration_min * 60 <= args.boot_timeout_s:
        raise SystemExit(
            "error: GPIO0 safety timer must exceed --boot-timeout-s; increase --duration-min"
        )
    if not shutil.which(args.uhubctl):
        raise SystemExit(f"error: uhubctl not found: {args.uhubctl}")

    port = resolve_port(args.port)
    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    out_dir = Path(args.out_dir) if args.out_dir else ROOT / "artifacts" / "qa" / f"lunasay-deep-sleep-{stamp}"
    out_dir.mkdir(parents=True, exist_ok=True)
    battery_photo_sha256: str | None = None
    battery_photo_artifact: str | None = None
    if args.battery_photo is not None:
        photo_source = args.battery_photo.resolve()
        photo_target = out_dir / f"battery-label{photo_source.suffix.lower()}"
        shutil.copy2(photo_source, photo_target)
        battery_photo_sha256 = hashlib.sha256(photo_target.read_bytes()).hexdigest()
        battery_photo_artifact = photo_target.name
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
            "time",
            "power",
            f"power deep-sleep {args.duration_min}",
            "power deep-sleep status",
        ],
        3.0,
    )
    (out_dir / "preflight-serial.log").write_text(preflight, encoding="utf-8")
    def cancel_preflight() -> None:
        try:
            serial_commands(port, ["power deep-sleep cancel"], 1.0)
        except Exception:
            pass

    if "deep-sleep pending=yes" not in preflight:
        cancel_preflight()
        raise RuntimeError("firmware did not arm deep sleep")
    if "time: valid=yes" not in preflight:
        cancel_preflight()
        raise RuntimeError("device wall clock is invalid; deep-sleep start time would be unavailable")
    preflight_power = parse_power_status(preflight)
    record("armed", duration_min=args.duration_min, wake_source=args.wake_source)

    power_off = False
    became_unreachable = False
    woke_on_network = False
    network_wake_after_s: float | None = None
    wake_battery: dict = {}
    run_error: str | None = None
    cleanup_error: str | None = None
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

        if args.wake_source == "timer":
            earliest_timer_wake = off_at + args.duration_min * 60.0
            if time.monotonic() < earliest_timer_wake:
                time.sleep(earliest_timer_wake - time.monotonic())
            wake_deadline = earliest_timer_wake + args.boot_timeout_s
        else:
            record("awaiting_gpio0", instruction="press the BOOT button once")
            wake_deadline = time.monotonic() + args.boot_timeout_s
        while time.monotonic() < wake_deadline:
            if ping(args.ip):
                woke_on_network = True
                network_wake_after_s = time.monotonic() - off_at
                record("network_wake", after_vbus_off_s=round(network_wake_after_s, 3))
                try:
                    wake_battery = wait_json_get(f"http://{args.ip}/api/battery")
                    record("wake_battery", sample=wake_battery)
                except Exception as exc:
                    record("wake_battery_unavailable", error=str(exc))
                break
            time.sleep(2.0)
    except Exception as exc:
        run_error = f"{type(exc).__name__}: {exc}"
        record("runner_error", error=run_error)
    finally:
        if power_off:
            try:
                set_hub_power(args.uhubctl, args.hub_location, args.hub_port, True)
                record("vbus_on")
                time.sleep(3.0)
            except Exception as exc:
                cleanup_error = f"VBUS restore failed: {type(exc).__name__}: {exc}"
                record("cleanup_failed", error=cleanup_error)

    postflight = ""
    try:
        port = wait_for_port(20.0)
        postflight = serial_commands(
            port,
            ["power deep-sleep status", "power", "power deep-sleep cancel"],
            3.0,
        )
    except Exception as exc:
        state_error = f"deep-sleep cleanup failed: {type(exc).__name__}: {exc}"
        cleanup_error = f"{cleanup_error}; {state_error}" if cleanup_error else state_error
        record("cleanup_failed", error=state_error)
    (out_dir / "postflight-serial.log").write_text(postflight, encoding="utf-8")
    if "power: deep-sleep cancelled" not in postflight:
        confirmation_error = "firmware did not confirm deep-sleep cancellation"
        cleanup_error = f"{cleanup_error}; {confirmation_error}" if cleanup_error else confirmation_error
        record("cleanup_failed", error=confirmation_error)
    retained = "retained=yes" in postflight
    completed = "completed=yes" in postflight
    wake_match = re.search(r"wake_cause=(\d+)", postflight)
    wake_cause = int(wake_match.group(1)) if wake_match else None
    # Public esp_sleep_wakeup_cause_t values: EXT0=2, TIMER=4.
    expected_wake_cause = 4 if args.wake_source == "timer" else 2
    expected_wake = wake_cause == expected_wake_cause
    deep_sleep_telemetry = wake_battery.get("deep_sleep", {})
    prepare_flags = int(deep_sleep_telemetry.get("prepare_flags", 0))
    serial_prepare_match = re.search(r"prepare_flags=0x([0-9a-fA-F]+)", postflight)
    serial_prepare_flags = int(serial_prepare_match.group(1), 16) if serial_prepare_match else 0
    prepare_valid = bool(
        (prepare_flags & 0x07) == 0x07
        and (serial_prepare_flags & 0x07) == 0x07
        and deep_sleep_telemetry.get("audio_quiesced")
        and deep_sleep_telemetry.get("display_quiesced")
        and deep_sleep_telemetry.get("pmu_quiesced")
    )
    telemetry_valid = bool(
        wake_battery.get("battery", {}).get("present")
        and deep_sleep_telemetry.get("completed")
        and deep_sleep_telemetry.get("wake_cause") == expected_wake_cause
        and prepare_valid
    )
    requested_s = args.duration_min * 60.0
    if args.wake_source == "timer":
        wake_timing_valid = bool(
            network_wake_after_s is not None
            and network_wake_after_s >= requested_s * 0.9
            and network_wake_after_s <= requested_s + args.boot_timeout_s
        )
    else:
        wake_timing_valid = bool(
            network_wake_after_s is not None and network_wake_after_s < requested_s * 0.9
        )
    passed = bool(
        run_error is None
        and cleanup_error is None
        and became_unreachable
        and woke_on_network
        and retained
        and completed
        and expected_wake
        and wake_timing_valid
        and telemetry_valid
    )
    firmware_article = firmware_provenance(
        wake_battery,
        preflight_power.get("firmware", "unknown"),
    )
    summary = {
        "passed": passed,
        "duration_min": args.duration_min,
        "boot_timeout_s": args.boot_timeout_s,
        "wake_source": args.wake_source,
        "became_unreachable": became_unreachable,
        "woke_on_network": woke_on_network,
        "network_wake_after_s": network_wake_after_s,
        "wake_timing_valid": wake_timing_valid,
        "wake_cause": wake_cause,
        "expected_wake_cause": expected_wake_cause,
        "expected_wake": expected_wake,
        "prepare_flags": prepare_flags,
        "serial_prepare_flags": serial_prepare_flags,
        "prepare_valid": prepare_valid,
        "telemetry_valid": telemetry_valid,
        "run_error": run_error,
        "cleanup_error": cleanup_error,
        "retained": retained,
        "completed": completed,
        "wake_battery": wake_battery,
        "test_article": {
            "unit_id": args.unit_id,
            "hardware_revision": args.hardware_revision.strip() or "unknown",
            "battery_id": args.battery_id,
            "battery_mah": args.battery_mah,
            "battery_photo": battery_photo_artifact,
            "battery_photo_sha256": battery_photo_sha256,
            "battery_cycle_count": args.battery_cycle_count,
            "ambient_c": args.ambient_c,
            "qualification_matrix_sha256": args.qualification_matrix_sha256 or None,
            "qualification_test_id": args.qualification_test_id or None,
            **firmware_article,
            "harness_build": harness_build,
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
