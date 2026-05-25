#!/usr/bin/env python3
"""Fetch NASA GIBS live-cloud Earth imagery and upload it to the P4 over serial."""

from __future__ import annotations

import argparse
import re
import sys
import time
import urllib.parse
import urllib.request
from datetime import date, timedelta
from io import BytesIO
from pathlib import Path

from PIL import Image
import serial


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUT = ROOT / ".cache" / "globe_live_clouds.png"
DEFAULT_URL = "https://gibs.earthdata.nasa.gov/wms/epsg4326/best/wms.cgi"


def gibs_url(width: int, day: date | None) -> str:
    params = {
        "SERVICE": "WMS",
        "VERSION": "1.1.1",
        "REQUEST": "GetMap",
        "LAYERS": "VIIRS_SNPP_CorrectedReflectance_TrueColor",
        "STYLES": "",
        "SRS": "EPSG:4326",
        "BBOX": "-180,-90,180,90",
        "WIDTH": str(width),
        "HEIGHT": str(width // 2),
        "FORMAT": "image/png",
        "TRANSPARENT": "FALSE",
    }
    if day is not None:
        params["TIME"] = day.isoformat()
    return f"{DEFAULT_URL}?{urllib.parse.urlencode(params)}"


def image_brightness(data: bytes) -> float:
    with Image.open(BytesIO(data)) as im:
        small = im.convert("RGB").resize((36, 18))
        return sum(sum(px) for px in small.getdata()) / (36 * 18 * 3)


def fetch_png(url: str, out: Path) -> tuple[bytes, float]:
    out.parent.mkdir(parents=True, exist_ok=True)
    req = urllib.request.Request(url, headers={"User-Agent": "Astrolabe-P4/1.0"})
    with urllib.request.urlopen(req, timeout=45) as resp:
        data = resp.read()
    if not data.startswith(b"\x89PNG\r\n\x1a\n"):
        raise RuntimeError("NASA response was not a PNG")
    brightness = image_brightness(data)
    out.write_bytes(data)
    return data, brightness


def upload(port: str, baud: int, data: bytes, timeout_s: float) -> None:
    with serial.Serial(port, baud, timeout=0.05, dsrdtr=False, rtscts=False) as ser:
        ser.setDTR(False)
        ser.setRTS(False)

        raw = b""
        deadline = time.time() + timeout_s
        while time.time() < deadline:
            raw += ser.read(4096)
            if b"Astrolabe P4 is running" in raw:
                break
        else:
            sys.stdout.write(raw.decode(errors="ignore"))
            raise TimeoutError("device did not finish booting")

        ser.reset_input_buffer()
        raw = b""
        ser.write(f"\nglobe put {len(data)}\n".encode())
        ser.flush()

        deadline = time.time() + timeout_s
        ready = re.compile(rb"GLOBE_PUT_READY\s+(\d+)")
        while time.time() < deadline:
            raw += ser.read(4096)
            m = ready.search(raw)
            if m:
                expected = int(m.group(1))
                if expected != len(data):
                    raise RuntimeError(f"device expected {expected} bytes, have {len(data)}")
                break
        else:
            sys.stdout.write(raw.decode(errors="ignore"))
            raise TimeoutError("device did not become ready for globe upload")

        ser.write(data)
        ser.flush()
        deadline = time.time() + timeout_s
        while time.time() < deadline:
            raw += ser.read(4096)
            if b"GLOBE_PUT_OK" in raw:
                sys.stdout.write(raw.decode(errors="ignore"))
                return
            if b"globe serial upload" in raw and b"failed" in raw:
                sys.stdout.write(raw.decode(errors="ignore"))
                raise RuntimeError("device rejected globe upload")
        sys.stdout.write(raw.decode(errors="ignore"))
        raise TimeoutError("device did not confirm globe upload")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="/dev/cu.wchusbserial5A360268091")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--width", type=int, default=720)
    parser.add_argument("--date", help="NASA GIBS date to fetch, YYYY-MM-DD. Defaults to latest recent non-black frame.")
    parser.add_argument("--lookback-days", type=int, default=14)
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT)
    parser.add_argument("--timeout", type=float, default=180)
    args = parser.parse_args()

    if args.width <= 0 or args.width % 2:
        raise ValueError("--width must be a positive even number")

    if args.date:
        days = [date.fromisoformat(args.date)]
    else:
        today = date.today()
        days = [today - timedelta(days=offset) for offset in range(1, args.lookback_days + 1)]

    data = b""
    chosen = None
    brightness = 0.0
    for day in days:
        url = gibs_url(args.width, day)
        data, brightness = fetch_png(url, args.out.expanduser().resolve())
        if brightness > 5.0:
            chosen = day
            break
    if chosen is None:
        raise RuntimeError("NASA GIBS returned only empty/dark frames")

    print(f"fetched {len(data)} bytes from NASA GIBS date={chosen.isoformat()} brightness={brightness:.1f}")
    print(f"cached {args.out}")
    upload(args.port, args.baud, data, args.timeout)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
