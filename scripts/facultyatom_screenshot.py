#!/usr/bin/env python3
"""Capture FacultyAtom (M5 AtomS3R) display over USB serial (screen command → 24-bit BMP)."""
from __future__ import annotations

import argparse
import re
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUT = ROOT / "artifacts" / "screens"

BEGIN_RE = re.compile(
    rb"screen: BEGIN w=(\d+) h=(\d+) bytes=(\d+)\s*$"
)
END_RE = re.compile(rb"screen: END\s*$")


def _open_serial(port: str, baud: int):
    try:
        import serial  # type: ignore
    except ImportError as exc:
        raise SystemExit(
            "pyserial missing — run: mcp/astrolabe-esp/setup.sh"
        ) from exc
    ser = serial.Serial()
    ser.port = port
    ser.baudrate = baud
    ser.timeout = 0.25
    # Opening USB Serial/JTAG with DTR asserted resets the S3 and loses the
    # screenshot command before the firmware's serial task is ready.
    ser.dtr = False
    ser.rts = False
    ser.open()
    return ser


def capture_screenshot(
    port: str,
    out_path: Path,
    *,
    baud: int = 115200,
    timeout_s: float = 15.0,
    command: str = "screen",
    settle_s: float = 0.15,
) -> Path:
    ser = _open_serial(port, baud)
    try:
        time.sleep(settle_s)
        ser.reset_input_buffer()
        ser.write((command.strip() + "\n").encode("utf-8"))
        ser.flush()

        deadline = time.time() + timeout_s
        expected: int | None = None
        bmp = bytearray()
        pending = bytearray()

        while time.time() < deadline:
            chunk = ser.read(4096 if expected is not None else 1)
            if not chunk:
                continue

            if expected is None:
                pending.extend(chunk)
                while b"\n" in pending:
                    line, _, rest = pending.partition(b"\n")
                    pending = bytearray(rest)
                    m = BEGIN_RE.search(line)
                    if m:
                        expected = int(m.group(3))
                        break
                    if line.startswith(b"screen: error"):
                        raise RuntimeError(line.decode("utf-8", errors="replace"))
                continue

            bmp.extend(chunk)
            if len(bmp) >= expected:
                bmp = bmp[:expected]
                break

        if expected is None:
            raise TimeoutError(f"no screen: BEGIN within {timeout_s:.0f}s")
        if len(bmp) != expected:
            raise RuntimeError(f"expected {expected} BMP bytes, got {len(bmp)}")

        tail_deadline = time.time() + 2.0
        tail = bytearray()
        while time.time() < tail_deadline:
            extra = ser.read(256)
            if not extra:
                continue
            tail.extend(extra)
            if END_RE.search(tail):
                break

        if bmp[:2] != b"BM":
            raise RuntimeError("response is not a BMP (missing BM header)")

        out_path.parent.mkdir(parents=True, exist_ok=True)
        out_path.write_bytes(bmp)
        return out_path
    finally:
        ser.close()


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Capture Atom Wand screen over serial")
    parser.add_argument(
        "-p",
        "--port",
        default="",
        help="serial port (default: first usbmodem)",
    )
    parser.add_argument(
        "-o",
        "--out",
        default="",
        help="output .bmp path (default: artifacts/screens/facultyatom-<utc>.bmp)",
    )
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=15.0)
    parser.add_argument(
        "--settle",
        type=float,
        default=0.15,
        help="seconds to wait after opening serial (use ~20 when the host resets USB Serial/JTAG)",
    )
    parser.add_argument(
        "--command",
        default="screen",
        help="serial command to send (default: screen)",
    )
    args = parser.parse_args(argv)

    port = args.port.strip()
    if not port:
        try:
            import serial.tools.list_ports  # type: ignore

            for info in serial.tools.list_ports.comports():
                dev = info.device
                if "usbmodem" in dev or "usbserial" in dev or "ACM" in dev:
                    port = dev
                    break
        except ImportError:
            pass
    if not port:
        raise SystemExit("error: pass -p PORT (no usbmodem port found)")

    if args.out.strip():
        out_path = Path(args.out).expanduser()
    else:
        stamp = datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%S")
        out_path = DEFAULT_OUT / f"facultyatom-{stamp}.bmp"

    path = capture_screenshot(
        port,
        out_path,
        baud=args.baud,
        timeout_s=args.timeout,
        command=args.command,
        settle_s=args.settle,
    )
    print(path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
