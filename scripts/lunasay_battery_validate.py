#!/usr/bin/env python3
"""Run attributable LunaSay battery and voice-endurance tests on a switched USB hub.

The firmware scenario is armed over USB, then hub VBUS is removed. Idle tests
avoid HTTP traffic until their final sample. Conversation tests use macOS
``say`` and the device's Wi-Fi API, recording each completed STT-LLM-TTS turn.
Journal tests record repeated transcription-only meeting segments and coverage.
Hub power is restored on every normal/error exit.
"""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
import glob
import json
import math
import os
from pathlib import Path
import shutil
import subprocess
import time
from urllib import error, request

import serial

from lunasay_power_common import wait_for_charge_ready


ROOT = Path(__file__).resolve().parents[1]
SCENARIOS = (
    "normal",
    "full-wifi",
    "full-offline",
    "dim-wifi",
    "dim-offline",
    "off-wifi",
    "sleep-offline",
)


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat()


def resolve_port(value: str) -> str:
    if value:
        return value
    ports = sorted(glob.glob("/dev/cu.usbmodem*") + glob.glob("/dev/ttyACM*"))
    if len(ports) != 1:
        raise RuntimeError(f"expected one USB serial device, found {ports}")
    return ports[0]


def hub_power(uhubctl: str, location: str, port: int, enabled: bool) -> None:
    subprocess.run(
        [uhubctl, "-l", location, "-p", str(port), "-a", "on" if enabled else "off"],
        check=True,
        stdout=subprocess.DEVNULL,
    )


def serial_commands(port: str, commands: list[str], settle_s: float = 2.0) -> str:
    with serial.Serial(port, 115200, timeout=0.2) as ser:
        ser.reset_input_buffer()
        ser.write(("\n".join(commands) + "\n").encode())
        ser.flush()
        time.sleep(settle_s)
        return ser.read_all().decode("utf-8", "replace")


def json_request(url: str, body: dict | None = None, timeout: float = 5.0) -> dict:
    data = json.dumps(body).encode() if body is not None else None
    req = request.Request(
        url,
        data=data,
        headers={"Content-Type": "application/json"} if data is not None else {},
        method="POST" if data is not None else "GET",
    )
    with request.urlopen(req, timeout=timeout) as response:
        return json.loads(response.read())


def wake_api(ip: str, timeout_s: float = 45.0) -> None:
    deadline = time.monotonic() + timeout_s
    last_error: Exception | None = None
    while time.monotonic() < deadline:
        for path in ("/", "/api/voice"):
            try:
                json_request(f"http://{ip}{path}" if path != "/" else f"http://{ip}/", timeout=2.5)
                if path == "/api/voice":
                    return
            except Exception as exc:  # thin listener intentionally closes its wake request
                last_error = exc
        time.sleep(1.0)
    raise RuntimeError(f"device API did not become ready: {last_error}")


def wait_voice_result(ip: str, baseline_sequence: int, timeout_s: float) -> dict:
    deadline = time.monotonic() + timeout_s
    last: dict = {}
    while time.monotonic() < deadline:
        try:
            last = json_request(f"http://{ip}/api/voice", timeout=4.0)
            if int(last.get("sequence", 0)) > baseline_sequence and not last.get("busy", True):
                return last
        except (OSError, ValueError, error.URLError):
            pass
        time.sleep(1.0)
    raise TimeoutError(f"voice turn did not finish; last status={last}")


def ble_probe(binary: Path, name_contains: str, timeout_s: float) -> dict:
    result = subprocess.run(
        [str(binary), "--name-contains", name_contains, "--timeout", str(timeout_s)],
        text=True,
        capture_output=True,
        timeout=timeout_s + 5.0,
    )
    try:
        payload = json.loads(result.stdout.strip())
    except (json.JSONDecodeError, ValueError):
        payload = {"found": False, "error": result.stderr.strip() or "invalid-probe-output"}
    payload["returncode"] = result.returncode
    return payload


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--scenario", choices=SCENARIOS, required=True)
    parser.add_argument("--duration-min", type=float, required=True)
    parser.add_argument("--workload", choices=("idle", "conversation", "journal", "ble"), default="idle")
    parser.add_argument("--ip", default="192.168.86.72")
    parser.add_argument("--port", default="")
    parser.add_argument("--hub-location", default="0-1.3")
    parser.add_argument("--hub-port", type=int, default=1)
    parser.add_argument("--uhubctl", default="/opt/homebrew/bin/uhubctl")
    parser.add_argument("--capture-ms", type=int, default=7000)
    parser.add_argument("--turn-interval-s", type=float, default=20.0)
    parser.add_argument("--journal-gap-s", type=float, default=2.0)
    parser.add_argument("--ble-name-contains", default="Astrolabe")
    parser.add_argument("--ble-probe-interval-s", type=float, default=60.0)
    parser.add_argument("--ble-probe-timeout-s", type=float, default=6.0)
    parser.add_argument("--turn-timeout-s", type=float, default=120.0)
    parser.add_argument("--say-rate", type=int, default=155)
    parser.add_argument("--say-volume", type=int, default=85)
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

    if args.duration_min <= 0:
        raise SystemExit("error: --duration-min must be positive")
    if not 1000 <= args.capture_ms <= 30000:
        raise SystemExit("error: --capture-ms must be 1000..30000")
    if args.turn_interval_s < 0 or args.journal_gap_s < 0:
        raise SystemExit("error: workload gaps must be non-negative")
    if args.ble_probe_interval_s <= 0 or args.ble_probe_timeout_s <= 0:
        raise SystemExit("error: BLE probe interval and timeout must be positive")
    if args.workload in ("conversation", "journal") and args.scenario not in ("full-wifi", "dim-wifi", "off-wifi"):
        raise SystemExit("error: voice workloads require full-wifi, dim-wifi, or off-wifi")
    if args.workload == "ble" and args.scenario not in ("full-offline", "dim-offline", "sleep-offline"):
        raise SystemExit("error: BLE workload requires full-offline, dim-offline, or sleep-offline")
    if not shutil.which(args.uhubctl):
        raise SystemExit(f"error: uhubctl not found: {args.uhubctl}")
    if args.workload in ("conversation", "journal") and not shutil.which("say"):
        raise SystemExit("error: voice workloads require macOS say")

    ble_probe_binary: Path | None = None
    if args.workload == "ble":
        swiftc = shutil.which("swiftc")
        if swiftc is None:
            raise SystemExit("error: BLE workload requires swiftc/CoreBluetooth")
        ble_probe_binary = Path("/tmp/lunasay_ble_probe")
        subprocess.run(
            [swiftc, str(ROOT / "scripts" / "lunasay_ble_probe.swift"), "-o", str(ble_probe_binary)],
            check=True,
        )

    port = resolve_port(args.port)
    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    out_dir = Path(args.out_dir) if args.out_dir else ROOT / "artifacts" / "qa" / f"lunasay-battery-{args.scenario}-{stamp}"
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
    started_wall = time.monotonic()
    deadline = started_wall + args.duration_min * 60.0
    firmware_minutes = min(10080, max(1, math.ceil(args.duration_min) + 5))
    events: list[dict] = []

    def record(kind: str, **fields: object) -> None:
        row = {"at": utc_now(), "elapsed_s": round(time.monotonic() - started_wall, 3), "kind": kind, **fields}
        events.append(row)
        with events_path.open("a", encoding="utf-8") as handle:
            handle.write(json.dumps(row) + "\n")
        print(json.dumps(row), flush=True)

    preflight = serial_commands(
        port,
        [
            "faces profile lunasay",
            *(
                ["faces set conversation"]
                if args.workload == "conversation"
                else (["faces set journal"] if args.workload == "journal" else [])
            ),
            *(["ble power-test on", "ble status"] if args.workload == "ble" else []),
            "power stream off",
            f"power scenario {args.scenario} {firmware_minutes}",
            "power",
        ],
        settle_s=3.0,
    )
    (out_dir / "preflight-serial.log").write_text(preflight, encoding="utf-8")
    if f"scenario={args.scenario}" not in preflight:
        if args.workload == "ble":
            serial_commands(port, ["ble power-test off"], settle_s=1.0)
        raise RuntimeError(f"firmware did not confirm scenario {args.scenario}")
    if args.workload == "ble" and "ble: power-test on ESP_OK" not in preflight:
        serial_commands(port, ["ble power-test off"], settle_s=1.0)
        raise RuntimeError("firmware did not enable BLE power-test mode")
    record("armed", scenario=args.scenario, workload=args.workload, duration_min=args.duration_min)

    original_volume: int | None = None
    power_off = False
    turns = 0
    successful_turns = 0
    final_battery: dict = {}
    device_unreachable = False
    try:
        if args.workload in ("conversation", "journal"):
            original_volume = int(subprocess.check_output(
                ["osascript", "-e", "output volume of (get volume settings)"], text=True
            ).strip())
            subprocess.run(["osascript", "-e", f"set volume output volume {args.say_volume}"], check=True)

        hub_power(args.uhubctl, args.hub_location, args.hub_port, False)
        power_off = True
        record("vbus_off")
        # Allow the PMU monitor to observe battery source and apply the profile
        # before an HTTP wake can alter the audio/settings runtime.
        time.sleep(8.0)

        if args.workload in ("idle", "ble"):
            wifi_scenario = args.scenario in ("full-wifi", "dim-wifi", "off-wifi")
            consecutive_ping_failures = 0
            consecutive_ble_failures = 0
            next_heartbeat = time.monotonic()
            next_ble_probe = time.monotonic()
            while time.monotonic() < deadline:
                sleep_s = min(30.0, max(0.0, deadline - time.monotonic()))
                if sleep_s > 0:
                    time.sleep(sleep_s)
                if args.workload == "ble":
                    if time.monotonic() < next_ble_probe:
                        continue
                    assert ble_probe_binary is not None
                    sample = ble_probe(ble_probe_binary, args.ble_name_contains, args.ble_probe_timeout_s)
                    next_ble_probe = time.monotonic() + args.ble_probe_interval_s
                    if sample.get("found"):
                        consecutive_ble_failures = 0
                        record("ble_alive", sample=sample)
                    else:
                        consecutive_ble_failures += 1
                        record("ble_probe_failed", consecutive=consecutive_ble_failures, sample=sample)
                        if consecutive_ble_failures >= 3:
                            device_unreachable = True
                            record("device_unreachable", probable_battery_shutdown=True, transport="ble")
                            break
                    continue
                if not wifi_scenario:
                    continue
                ping = subprocess.run(
                    ["ping", "-c", "1", "-W", "1000", args.ip],
                    stdout=subprocess.DEVNULL,
                    stderr=subprocess.DEVNULL,
                )
                if ping.returncode == 0:
                    consecutive_ping_failures = 0
                    if time.monotonic() >= next_heartbeat:
                        record("alive")
                        next_heartbeat = time.monotonic() + 15.0 * 60.0
                else:
                    consecutive_ping_failures += 1
                    record("ping_failed", consecutive=consecutive_ping_failures)
                    if consecutive_ping_failures >= 3:
                        device_unreachable = True
                        record("device_unreachable", probable_battery_shutdown=True)
                        break
        else:
            wake_api(args.ip)
            prompts = (
                (
                    "This is a meeting note about our LunaSay demonstration. We reviewed battery life, "
                    "voice clarity, and the next engineering milestone.",
                    "The group agreed to compare full brightness, dim display, and display off operation "
                    "using the same repeatable workload.",
                    "Our final note is that measured results should be conservative, reproducible, and "
                    "clearly labeled before publication.",
                )
                if args.workload == "journal"
                else (
                    "In one short sentence, tell me one interesting fact about the moon.",
                    "In one short sentence, suggest a thoughtful question for today.",
                    "In one short sentence, describe the night sky.",
                )
            )
            while time.monotonic() < deadline:
                baseline = json_request(f"http://{args.ip}/api/voice")
                phrase = prompts[turns % len(prompts)]
                accepted = json_request(
                    f"http://{args.ip}/api/voice",
                    {"action": "stt", "ms": args.capture_ms},
                )
                turns += 1
                turn_started = time.monotonic()
                if accepted.get("accepted"):
                    time.sleep(0.35)
                    subprocess.run(["say", "-r", str(args.say_rate), phrase], check=True)
                    result = wait_voice_result(args.ip, int(baseline.get("sequence", 0)), args.turn_timeout_s)
                else:
                    result = {"err": accepted.get("err", "not-accepted")}
                passed = bool(
                    result.get("err") == "ESP_OK"
                    and str(result.get("transcript", "")).strip()
                    and (args.workload == "journal" or str(result.get("reply", "")).strip())
                )
                successful_turns += int(passed)
                battery = json_request(f"http://{args.ip}/api/battery")
                final_battery = battery
                record(
                    "journal_segment" if args.workload == "journal" else "voice_turn",
                    turn=turns,
                    passed=passed,
                    wall_s=round(time.monotonic() - turn_started, 3),
                    prompt=phrase,
                    result=result,
                    battery=battery,
                )
                interval_s = args.journal_gap_s if args.workload == "journal" else args.turn_interval_s
                wait_s = min(interval_s, max(0.0, deadline - time.monotonic()))
                if wait_s > 0:
                    time.sleep(wait_s)
    finally:
        if power_off:
            hub_power(args.uhubctl, args.hub_location, args.hub_port, True)
            record("vbus_on")
            time.sleep(3.0)
        if original_volume is not None:
            subprocess.run(["osascript", "-e", f"set volume output volume {original_volume}"], check=False)

    postflight = serial_commands(
        port,
        ["power", "power scenario normal", *(["ble power-test off"] if args.workload == "ble" else [])],
        settle_s=2.0,
    )
    (out_dir / "postflight-serial.log").write_text(postflight, encoding="utf-8")
    if not final_battery:
        try:
            wake_api(args.ip)
            final_battery = json_request(f"http://{args.ip}/api/battery")
            record("postflight_battery", sample=final_battery)
        except Exception as exc:
            record("postflight_battery_unavailable", error=str(exc))
    endpoint = final_battery.get("battery", {}) if isinstance(final_battery, dict) else {}
    endpoint_percent = endpoint.get("percent")
    endpoint_voltage_mv = endpoint.get("voltage_mv")
    shutdown_confirmed = bool(
        device_unreachable
        and (
            (isinstance(endpoint_percent, (int, float)) and endpoint_percent <= 5)
            or (isinstance(endpoint_voltage_mv, (int, float)) and endpoint_voltage_mv <= 3400)
        )
    )
    if device_unreachable:
        record(
            "shutdown_endpoint",
            confirmed=shutdown_confirmed,
            percent=endpoint_percent,
            voltage_mv=endpoint_voltage_mv,
        )
    summary = {
        "passed": ((not device_unreachable) or shutdown_confirmed)
                  if args.workload == "idle"
                  else (turns > 0 and turns == successful_turns),
        "scenario": args.scenario,
        "workload": args.workload,
        "requested_duration_min": args.duration_min,
        "elapsed_s": round(time.monotonic() - started_wall, 3),
        "turns": turns,
        "successful_turns": successful_turns,
        "captured_audio_s": turns * args.capture_ms / 1000.0,
        "capture_coverage_ratio": min(
            1.0,
            (turns * args.capture_ms / 1000.0) / max(1.0, time.monotonic() - started_wall),
        ),
        "device_unreachable": device_unreachable,
        "shutdown_confirmed": shutdown_confirmed,
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
        "final_battery": final_battery,
        "preflight_serial": preflight,
        "postflight_serial": postflight,
        "events": len(events),
    }
    (out_dir / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(f"lunasay_battery_validate: {'PASS' if summary['passed'] else 'FAIL'} {out_dir}")
    return 0 if summary["passed"] else 2


if __name__ == "__main__":
    raise SystemExit(main())
