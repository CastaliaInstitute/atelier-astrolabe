#!/usr/bin/env python3
"""Pair a host with an Astrolabe remote-control endpoint."""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
import time
import urllib.request
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PAIR_RE = re.compile(r"remote: pairing code=(\d{6})")


def normalize_url(value: str) -> str:
    value = value.strip().rstrip("/")
    if not value.startswith(("http://", "https://")):
        value = f"http://{value}"
    return value


def get_code_from_serial(port: str, seconds: int) -> str:
    try:
        import serial
    except ModuleNotFoundError:
        venv_python = ROOT / "mcp" / "astrolabe-esp" / ".venv" / "bin" / "python"
        if venv_python.exists() and Path(sys.executable) != venv_python:
            os.execv(str(venv_python), [str(venv_python), *sys.argv])
        raise

    ser = serial.Serial(port, 115200, timeout=0.25)
    try:
        time.sleep(0.35)
        ser.reset_input_buffer()
        ser.write(f"remote pair {seconds}\n".encode("utf-8"))
        ser.flush()
        deadline = time.monotonic() + 8
        buf = ""
        while time.monotonic() < deadline:
            chunk = ser.read(4096)
            if chunk:
                text = chunk.decode("utf-8", errors="replace")
                print(text, end="", flush=True)
                buf += text
                match = PAIR_RE.search(buf)
                if match:
                    return match.group(1)
            time.sleep(0.05)
    finally:
        ser.close()
    raise RuntimeError("no pairing code printed by device")


def pair(url: str, code: str, label: str, timeout: float) -> dict:
    payload = json.dumps({"code": code, "label": label}, separators=(",", ":")).encode("utf-8")
    req = urllib.request.Request(
        f"{normalize_url(url)}/pair",
        data=payload,
        method="POST",
        headers={"Content-Type": "application/json", "Accept": "application/json"},
    )
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        data = json.loads(resp.read().decode("utf-8"))
        data["status"] = resp.status
        return data


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("url", help="Device URL or host, e.g. astrolabe-abcdef.local")
    parser.add_argument("--code", default="", help="Six-digit code printed by `remote pair`")
    parser.add_argument("--port", default=os.environ.get("ASTROLABE_SERIAL_PORT", ""))
    parser.add_argument("--seconds", type=int, default=120)
    parser.add_argument("--label", default=os.uname().nodename if hasattr(os, "uname") else "tour-host")
    parser.add_argument("--timeout", type=float, default=8.0)
    args = parser.parse_args()

    try:
        if not args.code and not args.port:
            raise RuntimeError("provide --code or --port")
        code = args.code or get_code_from_serial(args.port, args.seconds)
        result = pair(args.url, code, args.label, args.timeout)
    except Exception as exc:  # noqa: BLE001 - CLI should report the concrete pairing failure.
        print(f"remote_pair: {type(exc).__name__}: {exc}", file=sys.stderr)
        return 1
    if not result.get("ok") or not result.get("token"):
        print(json.dumps(result, indent=2), file=sys.stderr)
        return 1
    print(json.dumps({"url": normalize_url(args.url), "token": result["token"]}, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
