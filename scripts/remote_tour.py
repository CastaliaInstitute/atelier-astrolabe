#!/usr/bin/env python3
"""Run a scripted multi-device Astrolabe tour over the WiFi /control API."""

from __future__ import annotations

import argparse
import json
import os
import sys
import time
import urllib.error
import urllib.request
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import dataclass
from pathlib import Path
from string import Template
from typing import Any


ROOT = Path(__file__).resolve().parents[1]


@dataclass(frozen=True)
class Device:
    id: str
    url: str
    name: str
    role: str
    group: str
    token: str
    meta: dict[str, Any]


def load_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as f:
        data = json.load(f)
    if not isinstance(data, dict):
        raise ValueError(f"{path}: top-level JSON must be an object")
    return data


def normalize_url(value: str) -> str:
    value = value.strip().rstrip("/")
    if not value:
        raise ValueError("device url is empty")
    if not value.startswith(("http://", "https://")):
        value = f"http://{value}"
    return value


def load_devices(script: dict[str, Any], default_token: str) -> list[Device]:
    raw_devices = script.get("devices")
    if not isinstance(raw_devices, list) or not raw_devices:
        raise ValueError("script must define a non-empty devices array")
    devices: list[Device] = []
    for idx, raw in enumerate(raw_devices):
        if not isinstance(raw, dict):
            raise ValueError(f"devices[{idx}] must be an object")
        device_id = str(raw.get("id") or raw.get("name") or f"device{idx + 1}")
        url = normalize_url(str(raw.get("url") or raw.get("host") or ""))
        token = str(raw.get("token") or default_token)
        if not token:
            raise ValueError(f"device {device_id}: missing token; set --token or ASTROLABE_REMOTE_KEY")
        devices.append(
            Device(
                id=device_id,
                url=url,
                name=str(raw.get("name") or device_id),
                role=str(raw.get("role") or ""),
                group=str(raw.get("group") or ""),
                token=token,
                meta=raw,
            )
        )
    return devices


def select_devices(step: dict[str, Any], devices: list[Device]) -> list[Device]:
    target = step.get("target", "all")
    if target == "all":
        return devices
    targets = target if isinstance(target, list) else [target]
    wanted = {str(t) for t in targets}
    selected = [
        d
        for d in devices
        if d.id in wanted or d.name in wanted or (d.role and d.role in wanted) or (d.group and d.group in wanted)
    ]
    matched = {d.id for d in selected} | {d.name for d in selected} | {d.role for d in selected} | {
        d.group for d in selected
    }
    missing = sorted(wanted - matched)
    if missing:
        raise ValueError(f"unknown target(s): {', '.join(missing)}")
    return selected


def render_value(value: Any, device: Device, globals_: dict[str, Any]) -> Any:
    if isinstance(value, str):
        mapping = {
            "id": device.id,
            "name": device.name,
            "role": device.role,
            "group": device.group,
            **{f"device_{k}": v for k, v in device.meta.items() if isinstance(v, (str, int, float))},
            **{str(k): v for k, v in globals_.items() if isinstance(v, (str, int, float))},
        }
        return Template(value).safe_substitute(mapping)
    if isinstance(value, list):
        return [render_value(v, device, globals_) for v in value]
    if isinstance(value, dict):
        return {k: render_value(v, device, globals_) for k, v in value.items()}
    return value


def command_payload(step: dict[str, Any], device: Device, globals_: dict[str, Any]) -> dict[str, Any]:
    action = str(step.get("action") or step.get("cmd") or "").strip()
    if not action:
        raise ValueError("step missing action")
    if action == "say":
        action = "tts"
    payload: dict[str, Any] = {"cmd": action}
    for key in ("face", "button", "mode", "text", "x", "y", "durationMs", "dwellMs"):
        if key in step:
            payload[key] = render_value(step[key], device, globals_)
    if "duration_ms" in step:
        payload["durationMs"] = step["duration_ms"]
    if "dwell_ms" in step:
        payload["dwellMs"] = step["dwell_ms"]
    if action == "tts" and "text" not in payload:
        raise ValueError("tts step missing text")
    return payload


def post_control(device: Device, payload: dict[str, Any], timeout: float) -> dict[str, Any]:
    body = json.dumps(payload, separators=(",", ":")).encode("utf-8")
    req = urllib.request.Request(
        f"{device.url}/control",
        data=body,
        method="POST",
        headers={
            "Authorization": f"Bearer {device.token}",
            "Content-Type": "application/json",
            "Accept": "application/json",
        },
    )
    started = time.perf_counter()
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        raw = resp.read(4096)
        elapsed = round((time.perf_counter() - started) * 1000, 1)
        try:
            parsed = json.loads(raw.decode("utf-8"))
        except json.JSONDecodeError:
            parsed = {"raw": raw.decode("utf-8", errors="replace")}
        return {"status": resp.status, "elapsed_ms": elapsed, **parsed}


def run_command_step(
    step: dict[str, Any],
    step_idx: int,
    devices: list[Device],
    globals_: dict[str, Any],
    *,
    timeout: float,
    dry_run: bool,
) -> bool:
    selected = select_devices(step, devices)
    fanout = bool(step.get("parallel", True))
    payloads = [(d, command_payload(step, d, globals_)) for d in selected]
    label = step.get("label") or step.get("action") or step.get("cmd")
    print(f"[{step_idx:02d}] {label}: {', '.join(d.id for d, _ in payloads)}", flush=True)
    if dry_run:
        for device, payload in payloads:
            print(f"  DRY {device.id} {device.url}/control {json.dumps(payload, ensure_ascii=False)}", flush=True)
        return True

    ok = True

    def send(item: tuple[Device, dict[str, Any]]) -> tuple[Device, dict[str, Any], dict[str, Any] | Exception]:
        device, payload = item
        try:
            return device, payload, post_control(device, payload, timeout)
        except Exception as exc:  # noqa: BLE001 - tour logs need concrete transport failures.
            return device, payload, exc

    if fanout and len(payloads) > 1:
        with ThreadPoolExecutor(max_workers=min(len(payloads), 12)) as executor:
            futures = [executor.submit(send, item) for item in payloads]
            for future in as_completed(futures):
                device, payload, result = future.result()
                ok = print_result(device, payload, result) and ok
    else:
        for item in payloads:
            device, payload, result = send(item)
            ok = print_result(device, payload, result) and ok
    return ok


def print_result(device: Device, payload: dict[str, Any], result: dict[str, Any] | Exception) -> bool:
    if isinstance(result, Exception):
        print(f"  FAIL {device.id} {payload['cmd']}: {type(result).__name__}: {result}", flush=True)
        return False
    ok = result.get("ok") is True and int(result.get("status", 500)) < 400
    status = "OK" if ok else "FAIL"
    detail = f"seq={result.get('seq', '-')} {result.get('elapsed_ms', '-')}ms"
    if not ok:
        detail += f" error={result.get('error', result)}"
    print(f"  {status} {device.id} {payload['cmd']}: {detail}", flush=True)
    return ok


def run_script(script: dict[str, Any], devices: list[Device], args: argparse.Namespace) -> int:
    globals_ = script.get("vars") if isinstance(script.get("vars"), dict) else {}
    steps = script.get("steps")
    if not isinstance(steps, list) or not steps:
        raise ValueError("script must define a non-empty steps array")
    failed = False
    for idx, step_raw in enumerate(steps, start=1):
        if not isinstance(step_raw, dict):
            raise ValueError(f"steps[{idx - 1}] must be an object")
        step = step_raw
        action = str(step.get("action") or step.get("cmd") or "").strip()
        if action == "wait":
            # Deliberately allow wait-only steps without devices.
            seconds = float(step.get("seconds", 0))
            if "ms" in step:
                seconds = float(step["ms"]) / 1000.0
            print(f"[{idx:02d}] wait {seconds:.2f}s", flush=True)
            if not args.dry_run and seconds > 0:
                time.sleep(seconds)
            continue
        ok = run_command_step(
            step,
            idx,
            devices,
            globals_,
            timeout=args.timeout,
            dry_run=args.dry_run,
        )
        failed = failed or not ok
        pause = float(step.get("pause", 0))
        if not args.dry_run and pause > 0:
            time.sleep(pause)
        if failed and args.stop_on_error:
            return 1
    return 1 if failed else 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("script", type=Path, help="Tour JSON file")
    parser.add_argument("--token", default=os.environ.get("ASTROLABE_REMOTE_KEY", ""))
    parser.add_argument("--timeout", type=float, default=8.0)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--stop-on-error", action="store_true")
    args = parser.parse_args()

    try:
        script = load_json(args.script)
        devices = load_devices(script, args.token)
        return run_script(script, devices, args)
    except (OSError, ValueError, urllib.error.URLError) as exc:
        print(f"remote_tour: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
