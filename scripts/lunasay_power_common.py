"""Shared host-side controls for attributable LunaSay power tests."""

from __future__ import annotations

from datetime import datetime, timezone
import json
import math
from pathlib import Path
import re
import struct
import time
from typing import Callable


POWER_FIELD_RE = re.compile(r"\b([a-z_]+)=([^\s]+)")
ELF_SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
ESP_APP_DESC_MAGIC = 0xABCD5432


def image_app_identity(path: Path) -> dict:
    """Read the first ESP app descriptor without importing the IDF toolchain."""
    with path.open("rb") as handle:
        header = handle.read(24)
        segment_header = handle.read(8)
        descriptor = handle.read(176)
    if len(header) != 24 or len(segment_header) != 8 or len(descriptor) != 176:
        raise ValueError(f"firmware image is too short for an ESP app descriptor: {path}")
    magic = struct.unpack_from("<I", descriptor, 0)[0]
    if magic != ESP_APP_DESC_MAGIC:
        raise ValueError(f"firmware app descriptor magic is invalid: 0x{magic:08x}")
    decode = lambda data: data.split(b"\0", 1)[0].decode("utf-8", "strict")
    version = decode(descriptor[16:48])
    project = decode(descriptor[48:80])
    elf_sha256 = descriptor[144:176].hex()
    if not version or not project or not ELF_SHA256_RE.fullmatch(elf_sha256):
        raise ValueError("firmware app descriptor identity is incomplete")
    return {"project": project, "version": version, "elf_sha256": elf_sha256}


def expected_firmware_identity(path: Path, variant: str = "LunaSay") -> dict:
    """Return the exact API identity expected from a supplied app image."""
    return {**image_app_identity(path), "variant": variant}


def reported_firmware_identity(sample: dict) -> dict:
    """Extract the exact project/version/variant/ELF identity from battery API data."""
    firmware = sample.get("firmware", {}) if isinstance(sample, dict) else {}
    return {
        "project": firmware.get("project"),
        "version": firmware.get("version"),
        "variant": firmware.get("variant"),
        "elf_sha256": firmware.get("elf_sha256"),
    }


def firmware_provenance(sample: dict, fallback_version: object = "unknown") -> dict:
    firmware = sample.get("firmware", {}) if isinstance(sample, dict) else {}
    version = str(firmware.get("version", fallback_version) or "unknown").strip()
    variant = str(firmware.get("variant", "unknown") or "unknown").strip()
    elf_sha256 = str(firmware.get("elf_sha256", "") or "").strip().lower()
    complete = bool(
        version not in ("", "unknown")
        and variant not in ("", "unknown")
        and ELF_SHA256_RE.fullmatch(elf_sha256)
    )
    return {
        "firmware_build": (
            f"{version}|{variant}|{elf_sha256}" if complete else version
        ),
        "firmware_version": version,
        "firmware_variant": variant,
        "firmware_elf_sha256": elf_sha256 or None,
        "firmware_provenance_complete": complete,
    }


def parse_power_status(output: str) -> dict:
    lines = [line for line in output.splitlines() if line.startswith("power: ") and "percent=" in line]
    if not lines:
        raise RuntimeError(f"power status line missing from serial output: {output[-500:]}")
    fields = dict(POWER_FIELD_RE.findall(lines[-1]))
    for key in (
        "percent", "mv", "uptime_s", "reset_reason", "pmu_on", "pmu_off",
        "scenario_elapsed_s", "scenario_remaining_s", "scenario_battery_s",
        "scenario_awake_s", "scenario_breathing_s", "scenario_dimmed_s",
        "scenario_asleep_s", "scenario_wifi_s", "scenario_ble_s",
        "discharge_drop", "discharge_elapsed_s",
    ):
        if key in fields:
            fields[key] = int(fields[key], 0)
    return fields


def wait_for_charge_ready(
    sample: Callable[[], str],
    log_path: Path,
    rest_min: float,
    timeout_min: float,
    minimum_voltage_mv: int = 4100,
) -> dict:
    """Wait for charge termination and a continuous docked rest interval."""
    if (
        not math.isfinite(rest_min)
        or not math.isfinite(timeout_min)
        or rest_min < 0
        or timeout_min <= 0
        or not isinstance(minimum_voltage_mv, int)
        or isinstance(minimum_voltage_mv, bool)
        or not 3500 <= minimum_voltage_mv <= 4400
    ):
        raise ValueError("invalid charge gate timing or voltage")
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
            and status.get("battery") == "present"
            and status.get("charging") == "no"
            and int(status.get("percent", -1)) >= 99
            and minimum_voltage_mv <= int(status.get("mv", -1)) <= 4400
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
            "minimum_voltage_mv": minimum_voltage_mv,
            "maximum_voltage_mv": 4400,
            "status": status,
        }
        with log_path.open("a", encoding="utf-8") as handle:
            handle.write(json.dumps(row) + "\n")
        if charge_terminated and rested_s >= rest_min * 60.0:
            return row
        time.sleep(min(60.0, max(1.0, deadline - time.monotonic())))
    raise TimeoutError(f"battery did not reach charge-ready state; last={last}")
