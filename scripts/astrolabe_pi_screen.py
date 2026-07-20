#!/usr/bin/env python3
"""Stream a Raspberry Pi desktop to Astrolabe over USB CDC or Wi-Fi."""

from __future__ import annotations

import argparse
import io
import shutil
import subprocess
import sys
import time


def capture_jpeg(monitor: int, quality: int) -> bytes:
    try:
        from PIL import Image
    except ImportError as exc:
        raise SystemExit("Install capture dependency: python3 -m pip install pillow") from exc

    if shutil.which("grim") and __import__("os").environ.get("WAYLAND_DISPLAY"):
        shot = subprocess.run(["grim", "-t", "png", "-"], check=True, capture_output=True)
        image = Image.open(io.BytesIO(shot.stdout)).convert("RGB")
    else:
        try:
            import mss
        except ImportError as exc:
            raise SystemExit("Install X11 capture dependency: python3 -m pip install mss") from exc
        with mss.mss() as grabber:
            displays = grabber.monitors
            if monitor < 1 or monitor >= len(displays):
                raise SystemExit(f"monitor must be 1..{len(displays) - 1}")
            shot = grabber.grab(displays[monitor])
            image = Image.frombytes("RGB", shot.size, shot.rgb)

    width, height = image.size
    side = min(width, height)
    left = (width - side) // 2
    top = (height - side) // 2
    image = image.crop((left, top, left + side, top + side)).resize((466, 466), Image.Resampling.LANCZOS)
    output = io.BytesIO()
    image.save(output, "JPEG", quality=quality, optimize=False, progressive=False)
    payload = output.getvalue()
    if len(payload) > 256 * 1024:
        raise RuntimeError(f"encoded JPEG is too large: {len(payload)} bytes")
    return payload


class UsbTransport:
    def __init__(self, port: str) -> None:
        try:
            import serial
        except ImportError as exc:
            raise SystemExit("Install USB dependency: python3 -m pip install pyserial") from exc
        self.serial = serial.Serial(port, 115200, timeout=0.25, write_timeout=2)

    def send(self, jpeg: bytes) -> None:
        self.serial.write(f"screen put jpeg {len(jpeg)}\n".encode())
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            line = self.serial.readline()
            if line.startswith(b"screen: READY"):
                break
        else:
            raise TimeoutError("Astrolabe did not acknowledge the screen frame")
        # The ESP32-S3 CDC receive ring is intentionally small to preserve
        # internal DMA RAM. Pace the frame so a desktop USB host cannot fill
        # that ring faster than the firmware drains it.
        for offset in range(0, len(jpeg), 512):
            self.serial.write(jpeg[offset : offset + 512])
            self.serial.flush()
            time.sleep(0.002)
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            line = self.serial.readline()
            if line.startswith(b"screen: OK"):
                return
            if line.startswith(b"screen: ERROR"):
                raise RuntimeError(line.decode("utf-8", "replace").strip())
        raise TimeoutError("Astrolabe did not finish the screen frame")


class WifiTransport:
    def __init__(self, url: str, pair: str) -> None:
        try:
            import requests
        except ImportError as exc:
            raise SystemExit("Install Wi-Fi dependency: python3 -m pip install requests") from exc
        self.requests = requests
        self.url = url.rstrip("/") + "/api/screen/jpeg"
        self.headers = {"X-Astrolabe-Pair": pair, "Content-Type": "image/jpeg"}

    def send(self, jpeg: bytes) -> None:
        response = self.requests.post(self.url, data=jpeg, headers=self.headers, timeout=3)
        response.raise_for_status()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    transport = parser.add_mutually_exclusive_group(required=True)
    transport.add_argument("--usb", metavar="PORT", help="Astrolabe CDC port, e.g. /dev/ttyACM0")
    transport.add_argument("--wifi", metavar="URL", help="Astrolabe base URL, e.g. http://192.168.1.42")
    parser.add_argument("--pair", help="six-digit on-device code; required with --wifi")
    parser.add_argument("--fps", type=float, default=4.0)
    parser.add_argument("--quality", type=int, default=58)
    parser.add_argument("--monitor", type=int, default=1)
    parser.add_argument("--once", action="store_true")
    args = parser.parse_args()

    if args.fps <= 0 or args.fps > 15:
        parser.error("--fps must be between 0 and 15")
    if args.quality < 25 or args.quality > 90:
        parser.error("--quality must be between 25 and 90")
    if args.wifi and (args.pair is None or len(args.pair) != 6 or not args.pair.isdigit()):
        parser.error("--wifi requires --pair with the six-digit code shown on Astrolabe")

    sender = UsbTransport(args.usb) if args.usb else WifiTransport(args.wifi, args.pair)
    interval = 1.0 / args.fps
    sent = 0
    try:
        while True:
            started = time.monotonic()
            jpeg = capture_jpeg(args.monitor, args.quality)
            sender.send(jpeg)
            sent += 1
            if args.once:
                break
            time.sleep(max(0, interval - (time.monotonic() - started)))
    except KeyboardInterrupt:
        pass
    except Exception as exc:
        print(f"astrolabe_pi_screen: {exc}", file=sys.stderr)
        return 1
    print(f"astrolabe_pi_screen: sent {sent} frame(s)", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
