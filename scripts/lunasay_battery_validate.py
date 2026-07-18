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
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import subprocess
import time
from urllib import error, request

import serial

from lunasay_power_common import parse_power_status, wait_for_charge_ready


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
BLE_WORKLOADS = ("ble", "ble-config")
VOICE_WORKLOADS = ("conversation", "journal")


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat()


def resolve_port(value: str) -> str:
    if value:
        return value
    ports = sorted(glob.glob("/dev/cu.usbmodem*") + glob.glob("/dev/ttyACM*"))
    if len(ports) != 1:
        raise RuntimeError(f"expected one USB serial device, found {ports}")
    return ports[0]


def wait_for_port(timeout_s: float) -> str:
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        ports = sorted(glob.glob("/dev/cu.usbmodem*") + glob.glob("/dev/ttyACM*"))
        if len(ports) == 1:
            return ports[0]
        time.sleep(0.5)
    raise RuntimeError("USB serial did not return after VBUS restore")


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


def ble_probe(binary: Path, name_contains: str, timeout_s: float, operation: str) -> dict:
    result = subprocess.run(
        [
            str(binary),
            "--name-contains", name_contains,
            "--timeout", str(timeout_s),
            "--operation", operation,
        ],
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


def validate_scenario_evidence(
    scenario: str,
    workload: str,
    status: dict,
    battery_elapsed_s: float,
    history: list[dict] | None = None,
    battery_started_epoch: float | None = None,
    battery_ended_epoch: float | None = None,
) -> dict:
    """Prove display/radio usage from battery-only firmware counters."""
    battery_s = float(status.get("scenario_battery_s", 0))
    mode_key = (
        "scenario_dimmed_s"
        if scenario in ("dim-wifi", "dim-offline")
        else "scenario_asleep_s"
        if scenario in ("off-wifi", "sleep-offline")
        else "scenario_awake_s"
    )
    mode_s = float(status.get(mode_key, 0))
    wifi_s = float(status.get("scenario_wifi_s", 0))
    ble_s = float(status.get("scenario_ble_s", 0))
    wifi_expected = scenario in ("full-wifi", "dim-wifi", "off-wifi")
    ble_expected = workload in BLE_WORKLOADS
    coverage_ratio = battery_s / battery_elapsed_s if battery_elapsed_s > 0 else 0.0
    mode_ratio = mode_s / battery_s if battery_s > 0 else 0.0
    wifi_ratio = wifi_s / battery_s if battery_s > 0 else 0.0
    ble_ratio = ble_s / battery_s if battery_s > 0 else 0.0
    counter_checks = {
        "scenario_name": status.get("scenario") == scenario,
        "battery_counter_coverage": coverage_ratio >= 0.80,
        "display_mode_dominant": mode_ratio >= 0.90,
        "wifi_state": wifi_ratio >= 0.75 if wifi_expected else (wifi_s <= 5 or wifi_ratio <= 0.05),
        "ble_state": ble_ratio >= 0.75 if ble_expected else (ble_s <= 5 or ble_ratio <= 0.05),
    }
    expected_mode = (
        "dimmed"
        if scenario in ("dim-wifi", "dim-offline")
        else "asleep"
        if scenario in ("off-wifi", "sleep-offline")
        else "awake"
    )
    metadata_samples = []
    for sample in history or []:
        epoch_s = float(sample.get("epoch_s", 0))
        if battery_started_epoch is not None and epoch_s < battery_started_epoch - 5:
            continue
        if battery_ended_epoch is not None and epoch_s > battery_ended_epoch + 5:
            continue
        if (
            sample.get("battery_present")
            and not sample.get("vbus")
            and not sample.get("charging")
            and isinstance(sample.get("mode"), str)
            and isinstance(sample.get("scenario"), str)
        ):
            metadata_samples.append(sample)
    matching_samples = [
        sample for sample in metadata_samples
        if sample.get("scenario") == scenario
        and sample.get("mode") == expected_mode
        and bool(sample.get("wifi")) == wifi_expected
        and bool(sample.get("ble")) == ble_expected
    ]
    history_ratio = len(matching_samples) / len(metadata_samples) if metadata_samples else 0.0
    history_passed = len(metadata_samples) >= 2 and history_ratio >= 0.80
    counter_passed = all(counter_checks.values())
    return {
        "passed": counter_passed or history_passed,
        "counter_passed": counter_passed,
        "counter_checks": counter_checks,
        "history_passed": history_passed,
        "history_samples": len(metadata_samples),
        "history_matching_samples": len(matching_samples),
        "history_match_ratio": round(history_ratio, 4),
        "expected": {
            "scenario": scenario,
            "display_mode": expected_mode,
            "display_counter": mode_key,
            "wifi": wifi_expected,
            "ble": ble_expected,
        },
        "seconds": {
            "runner_battery": round(battery_elapsed_s, 3),
            "firmware_battery": battery_s,
            "display_mode": mode_s,
            "wifi": wifi_s,
            "ble": ble_s,
        },
        "ratios": {
            "counter_coverage": round(coverage_ratio, 4),
            "display_mode": round(mode_ratio, 4),
            "wifi": round(wifi_ratio, 4),
            "ble": round(ble_ratio, 4),
        },
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--scenario", choices=SCENARIOS, required=True)
    parser.add_argument("--duration-min", type=float, required=True)
    parser.add_argument(
        "--workload",
        choices=("idle", "conversation", "journal", *BLE_WORKLOADS),
        default="idle",
    )
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
    parser.add_argument("--ble-config-write-interval-s", type=float, default=3600.0)
    parser.add_argument("--turn-timeout-s", type=float, default=120.0)
    parser.add_argument("--say-rate", type=int, default=155)
    parser.add_argument("--say-volume", type=int, default=85)
    parser.add_argument("--out-dir", default="")
    parser.add_argument("--unit-id", default="dev-unit-1")
    parser.add_argument("--hardware-revision", default="")
    parser.add_argument("--battery-id", default="unlabeled")
    parser.add_argument("--battery-mah", type=float, default=None)
    parser.add_argument("--battery-photo", type=Path, default=None)
    parser.add_argument("--battery-cycle-count", type=int, default=None)
    parser.add_argument("--ambient-c", type=float, default=None)
    parser.add_argument("--rest-min", type=float, default=30.0)
    parser.add_argument("--charge-ready-timeout-min", type=float, default=360.0)
    parser.add_argument("--allow-not-ready", action="store_true",
                        help="skip full-charge/rest gate; smoke tests only")
    args = parser.parse_args()
    harness_build = subprocess.check_output(
        ["git", "describe", "--always", "--dirty"], cwd=ROOT, text=True
    ).strip()

    if args.duration_min <= 0:
        raise SystemExit("error: --duration-min must be positive")
    if args.battery_mah is not None and args.battery_mah <= 0:
        raise SystemExit("error: --battery-mah must be positive")
    if args.battery_cycle_count is not None and args.battery_cycle_count < 0:
        raise SystemExit("error: --battery-cycle-count must be non-negative")
    if args.battery_photo is not None and not args.battery_photo.is_file():
        raise SystemExit(f"error: battery label photo not found: {args.battery_photo}")
    if not args.allow_not_ready and (
        args.battery_mah is None
        or args.battery_photo is None
        or not args.hardware_revision.strip()
        or args.ambient_c is None
    ):
        raise SystemExit(
            "error: qualified runs require --battery-mah, --battery-photo, "
            "--hardware-revision, and --ambient-c"
        )
    if not 1000 <= args.capture_ms <= 30000:
        raise SystemExit("error: --capture-ms must be 1000..30000")
    if args.turn_interval_s < 0 or args.journal_gap_s < 0:
        raise SystemExit("error: workload gaps must be non-negative")
    if (
        args.ble_probe_interval_s <= 0
        or args.ble_probe_timeout_s <= 0
        or args.ble_config_write_interval_s <= 0
    ):
        raise SystemExit("error: BLE probe, timeout, and configuration-write intervals must be positive")
    if args.workload in VOICE_WORKLOADS and args.scenario not in ("full-wifi", "dim-wifi", "off-wifi"):
        raise SystemExit("error: voice workloads require full-wifi, dim-wifi, or off-wifi")
    if args.workload in BLE_WORKLOADS and args.scenario not in ("full-offline", "dim-offline", "sleep-offline"):
        raise SystemExit("error: BLE workload requires full-offline, dim-offline, or sleep-offline")
    if not shutil.which(args.uhubctl):
        raise SystemExit(f"error: uhubctl not found: {args.uhubctl}")
    if args.workload in VOICE_WORKLOADS and not shutil.which("say"):
        raise SystemExit("error: voice workloads require macOS say")

    ble_probe_binary: Path | None = None
    if args.workload in BLE_WORKLOADS:
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
            *(["ble power-test on", "ble status"] if args.workload in BLE_WORKLOADS else []),
            "power stream off",
            f"power scenario {args.scenario} {firmware_minutes}",
            "time",
            "power",
        ],
        settle_s=3.0,
    )
    (out_dir / "preflight-serial.log").write_text(preflight, encoding="utf-8")
    def cancel_preflight() -> None:
        commands = [
            "power scenario normal",
            *(["ble power-test off"] if args.workload in BLE_WORKLOADS else []),
        ]
        try:
            serial_commands(port, commands, settle_s=1.0)
        except Exception:
            pass

    if f"scenario={args.scenario}" not in preflight:
        cancel_preflight()
        raise RuntimeError(f"firmware did not confirm scenario {args.scenario}")
    if "time: valid=yes" not in preflight:
        cancel_preflight()
        raise RuntimeError("device wall clock is invalid; retained battery history would be unusable")
    preflight_power = parse_power_status(preflight)
    if args.workload in BLE_WORKLOADS and "ble: power-test on ESP_OK" not in preflight:
        cancel_preflight()
        raise RuntimeError("firmware did not enable BLE power-test mode")
    record("armed", scenario=args.scenario, workload=args.workload, duration_min=args.duration_min)

    original_volume: int | None = None
    power_off = False
    turns = 0
    successful_turns = 0
    accepted_captures = 0
    captured_audio_s = 0.0
    ble_probes = 0
    successful_ble_probes = 0
    ble_config_roundtrips = 0
    final_battery: dict = {}
    device_unreachable = False
    run_error: str | None = None
    cleanup_error: str | None = None
    postflight = ""
    battery_started_at: float | None = None
    battery_ended_at: float | None = None
    battery_started_epoch: float | None = None
    battery_ended_epoch: float | None = None
    try:
        if args.workload in VOICE_WORKLOADS:
            original_volume = int(subprocess.check_output(
                ["osascript", "-e", "output volume of (get volume settings)"], text=True
            ).strip())
            subprocess.run(["osascript", "-e", f"set volume output volume {args.say_volume}"], check=True)

        hub_power(args.uhubctl, args.hub_location, args.hub_port, False)
        power_off = True
        battery_started_at = time.monotonic()
        battery_started_epoch = time.time()
        deadline = battery_started_at + args.duration_min * 60.0
        record("vbus_off")
        # Allow the PMU monitor to observe battery source and apply the profile
        # before an HTTP wake can alter the audio/settings runtime.
        time.sleep(8.0)

        if args.workload in ("idle", *BLE_WORKLOADS):
            wifi_scenario = args.scenario in ("full-wifi", "dim-wifi", "off-wifi")
            consecutive_ping_failures = 0
            consecutive_ble_failures = 0
            next_heartbeat = time.monotonic()
            next_ble_probe = time.monotonic()
            next_ble_config_write = time.monotonic()
            while time.monotonic() < deadline:
                sleep_s = min(30.0, max(0.0, deadline - time.monotonic()))
                if sleep_s > 0:
                    time.sleep(sleep_s)
                if args.workload in BLE_WORKLOADS:
                    if time.monotonic() < next_ble_probe:
                        continue
                    assert ble_probe_binary is not None
                    if args.workload == "ble-config":
                        if time.monotonic() >= next_ble_config_write:
                            operation = "settings-roundtrip"
                            next_ble_config_write = time.monotonic() + args.ble_config_write_interval_s
                        else:
                            operation = "settings-read"
                    else:
                        operation = "advertise"
                    probe_started = time.monotonic()
                    sample = ble_probe(
                        ble_probe_binary,
                        args.ble_name_contains,
                        args.ble_probe_timeout_s,
                        operation,
                    )
                    probe_wall_s = time.monotonic() - probe_started
                    ble_probes += 1
                    next_ble_probe = time.monotonic() + args.ble_probe_interval_s
                    probe_passed = bool(
                        sample.get("found")
                        and (
                            args.workload != "ble-config"
                            or sample.get("roundtrip")
                            or sample.get("settings_read")
                        )
                    )
                    if probe_passed:
                        successful_ble_probes += 1
                        ble_config_roundtrips += int(bool(sample.get("roundtrip")))
                        consecutive_ble_failures = 0
                        record(
                            (
                                "ble_config_roundtrip"
                                if sample.get("roundtrip")
                                else "ble_settings_read"
                                if sample.get("settings_read")
                                else "ble_alive"
                            ),
                            wall_s=round(probe_wall_s, 3),
                            sample=sample,
                        )
                    else:
                        consecutive_ble_failures += 1
                        record(
                            "ble_probe_failed",
                            consecutive=consecutive_ble_failures,
                            wall_s=round(probe_wall_s, 3),
                            sample=sample,
                        )
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
                    accepted_captures += 1
                    captured_audio_s += args.capture_ms / 1000.0
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
                    accepted=bool(accepted.get("accepted")),
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
    except Exception as exc:
        run_error = f"{type(exc).__name__}: {exc}"
        record("runner_error", error=run_error)
    finally:
        if power_off:
            try:
                hub_power(args.uhubctl, args.hub_location, args.hub_port, True)
                battery_ended_at = time.monotonic()
                battery_ended_epoch = time.time()
                record("vbus_on")
                time.sleep(3.0)
            except Exception as exc:
                cleanup_error = f"VBUS restore failed: {type(exc).__name__}: {exc}"
                record("cleanup_failed", error=cleanup_error)
        if original_volume is not None:
            subprocess.run(["osascript", "-e", f"set volume output volume {original_volume}"], check=False)
        try:
            restored_port = wait_for_port(20.0)
            postflight = serial_commands(
                restored_port,
                [
                    "power",
                    "power scenario normal",
                    *(["ble power-test off"] if args.workload in BLE_WORKLOADS else []),
                ],
                settle_s=2.0,
            )
        except Exception as exc:
            state_error = f"firmware-state cleanup failed: {type(exc).__name__}: {exc}"
            cleanup_error = f"{cleanup_error}; {state_error}" if cleanup_error else state_error
            record("cleanup_failed", error=state_error)
    (out_dir / "postflight-serial.log").write_text(postflight, encoding="utf-8")
    cleanup_checks = []
    if "power: scenario=normal" not in postflight:
        cleanup_checks.append("firmware did not confirm normal power scenario")
    if args.workload in BLE_WORKLOADS and "ble: power-test off ESP_OK" not in postflight:
        cleanup_checks.append("firmware did not confirm BLE power-test shutdown")
    if cleanup_checks:
        confirmation_error = "; ".join(cleanup_checks)
        cleanup_error = f"{cleanup_error}; {confirmation_error}" if cleanup_error else confirmation_error
        record("cleanup_failed", error=confirmation_error)
    try:
        postflight_power = parse_power_status(postflight)
    except Exception as exc:
        postflight_power = {}
        parse_error = f"postflight power telemetry invalid: {type(exc).__name__}: {exc}"
        cleanup_error = f"{cleanup_error}; {parse_error}" if cleanup_error else parse_error
        record("cleanup_failed", error=parse_error)
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
    postflight_uptime_s = int(postflight_power.get("uptime_s", 0))
    expected_uptime_s = int(preflight_power.get("uptime_s", 0)) + int(time.monotonic() - started_wall)
    reboot_confirmed = postflight_uptime_s + 120 < expected_uptime_s
    poweron_reset = int(postflight_power.get("reset_reason", 0)) == 1
    pmu_under_voltage = bool(int(postflight_power.get("pmu_off", 0)) & (1 << 3))
    shutdown_confirmed = bool(
        device_unreachable
        and (
            (isinstance(endpoint_percent, (int, float)) and endpoint_percent <= 5)
            or (isinstance(endpoint_voltage_mv, (int, float)) and endpoint_voltage_mv <= 3400)
            or (reboot_confirmed and poweron_reset and pmu_under_voltage)
        )
    )
    if device_unreachable:
        record(
            "shutdown_endpoint",
            confirmed=shutdown_confirmed,
            percent=endpoint_percent,
            voltage_mv=endpoint_voltage_mv,
            reboot_confirmed=reboot_confirmed,
            poweron_reset=poweron_reset,
            pmu_under_voltage=pmu_under_voltage,
        )
    transport_passed = (not device_unreachable) or shutdown_confirmed
    voice_passed = args.workload not in VOICE_WORKLOADS or (turns > 0 and turns == successful_turns)
    ble_passed = args.workload not in BLE_WORKLOADS or (
        successful_ble_probes > 0
        and (successful_ble_probes == ble_probes or shutdown_confirmed)
    )
    battery_elapsed_s = (
        max(0.0, battery_ended_at - battery_started_at)
        if battery_started_at is not None and battery_ended_at is not None
        else 0.0
    )
    scenario_evidence = validate_scenario_evidence(
        args.scenario,
        args.workload,
        postflight_power,
        battery_elapsed_s,
        final_battery.get("history", []) if isinstance(final_battery, dict) else [],
        battery_started_epoch,
        battery_ended_epoch,
    )
    if not scenario_evidence["passed"]:
        record("scenario_evidence_failed", evidence=scenario_evidence)
    summary = {
        "passed": (
            run_error is None
            and cleanup_error is None
            and transport_passed
            and voice_passed
            and ble_passed
            and scenario_evidence["passed"]
        ),
        "scenario": args.scenario,
        "workload": args.workload,
        "requested_duration_min": args.duration_min,
        "elapsed_s": round(time.monotonic() - started_wall, 3),
        "battery_elapsed_s": round(battery_elapsed_s, 3),
        "turns": turns,
        "successful_turns": successful_turns,
        "accepted_captures": accepted_captures,
        "ble_probes": ble_probes,
        "successful_ble_probes": successful_ble_probes,
        "ble_config_roundtrips": ble_config_roundtrips,
        "captured_audio_s": captured_audio_s,
        "capture_coverage_ratio": min(
            1.0,
            captured_audio_s / max(1.0, battery_elapsed_s),
        ),
        "device_unreachable": device_unreachable,
        "shutdown_confirmed": shutdown_confirmed,
        "run_error": run_error,
        "cleanup_error": cleanup_error,
        "scenario_evidence": scenario_evidence,
        "shutdown_evidence": {
            "endpoint_percent": endpoint_percent,
            "endpoint_voltage_mv": endpoint_voltage_mv,
            "reboot_confirmed": reboot_confirmed,
            "poweron_reset": poweron_reset,
            "pmu_under_voltage": pmu_under_voltage,
            "preflight_uptime_s": preflight_power.get("uptime_s"),
            "postflight_uptime_s": postflight_uptime_s,
        },
        "test_article": {
            "unit_id": args.unit_id,
            "hardware_revision": args.hardware_revision.strip() or "unknown",
            "battery_id": args.battery_id,
            "battery_mah": args.battery_mah,
            "battery_photo": battery_photo_artifact,
            "battery_photo_sha256": battery_photo_sha256,
            "battery_cycle_count": args.battery_cycle_count,
            "ambient_c": args.ambient_c,
            "firmware_build": final_battery.get("firmware", {}).get(
                "version", preflight_power.get("firmware", "unknown")
            ),
            "harness_build": harness_build,
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
