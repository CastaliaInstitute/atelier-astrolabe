#!/usr/bin/env python3
"""Set Astrolabe Faculty watch time over the serial command console."""

from __future__ import annotations

import argparse
import glob
import os
import sys
import time

try:
    import serial
except ImportError as exc:  # pragma: no cover - environment guard
    raise SystemExit("error: pyserial is required for astrolabe175c_set_time.py") from exc


DEFAULT_TZ = "PST8PDT,M3.2.0,M11.1.0"
DEFAULT_BAUD = 115200


def resolve_port(requested: str | None) -> str:
    if requested:
        return requested
    for env_name in ("ASTROLABE175C_TIME_PORT", "ASTROLABE_UPLOAD_PORT", "UPLOAD_PORT", "ESPPORT", "IDF_PORT"):
        value = os.environ.get(env_name)
        if value:
            return value
    preferred = "/dev/cu.usbmodem11301"
    if os.path.exists(preferred):
        return preferred
    ports = sorted(glob.glob("/dev/cu.usbmodem*"))
    if len(ports) == 1:
        return ports[0]
    if not ports:
        raise SystemExit("error: no /dev/cu.usbmodem* serial ports found")
    raise SystemExit("error: multiple serial ports found; pass --port or set ASTROLABE175C_TIME_PORT")


def read_until_quiet(ser: serial.Serial, quiet_s: float = 0.35, max_s: float = 4.0) -> str:
    end = time.time() + max_s
    quiet_until = time.time() + quiet_s
    chunks: list[bytes] = []
    while time.time() < end:
        chunk = ser.read(4096)
        if chunk:
            chunks.append(chunk)
            quiet_until = time.time() + quiet_s
        elif time.time() >= quiet_until:
            break
    return b"".join(chunks).decode("utf-8", "replace")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="serial port, defaults to ASTROLABE175C_TIME_PORT/ESPPORT/11301")
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD)
    parser.add_argument("--tz", default=os.environ.get("ASTROLABE175C_POSIX_TZ", DEFAULT_TZ))
    parser.add_argument("--epoch", type=int, default=int(time.time()))
    parser.add_argument("--boot-wait", type=float, default=2.5)
    args = parser.parse_args()

    port = resolve_port(args.port)
    time.sleep(max(0.0, args.boot_wait))
    with serial.Serial(port, args.baud, timeout=0.2) as ser:
        ser.reset_input_buffer()
        for command in (
            f"time tz {args.tz}",
            f"time set {args.epoch}",
            "time status",
        ):
            ser.write((command + "\r\n").encode("utf-8"))
            ser.flush()
            time.sleep(0.25)
        output = read_until_quiet(ser)

    sys.stdout.write(output)
    if "time: valid=yes" not in output or "ESP_OK" not in output:
        raise SystemExit("error: watch did not confirm valid time")
    print(f"astrolabe175c_set_time: synced port={port} epoch={args.epoch} tz={args.tz}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
