#!/usr/bin/env python3
"""Capture all Astrolabe face screenshots over USB serial `qa screen64`."""

from __future__ import annotations

import argparse
import base64
import re
import sys
import time
from datetime import datetime
from pathlib import Path

import serial


ROOT = Path(__file__).resolve().parents[1]
CRASH_RE = re.compile(r"Guru Meditation|Backtrace:|Stack canary|stack overflow|panic|Brownout|ASTROLABE_ALERT", re.I)
FACE_RE = re.compile(r"^qa:\s+(\d+)\s+(.+?)\s+enum=(\d+)\s*$")
B64_RE = re.compile(r"^[A-Za-z0-9+/]+={0,2}$")


def read_lines(ser: serial.Serial, seconds: float) -> list[str]:
    deadline = time.monotonic() + seconds
    lines: list[str] = []
    buf = b""
    while time.monotonic() < deadline:
        chunk = ser.read(4096)
        if not chunk:
            continue
        buf += chunk
        while b"\n" in buf:
            raw, buf = buf.split(b"\n", 1)
            lines.append(raw.decode("utf-8", errors="replace").rstrip("\r"))
    if buf:
        lines.append(buf.decode("utf-8", errors="replace").rstrip("\r"))
    return lines


def send_line(ser: serial.Serial, line: str) -> None:
    ser.write((line + "\n").encode("utf-8"))
    ser.flush()


def get_faces(ser: serial.Serial) -> list[dict]:
    ser.reset_input_buffer()
    send_line(ser, "qa faces")
    faces: list[dict] = []
    for line in read_lines(ser, 4.0):
        print(line, flush=True)
        if CRASH_RE.search(line):
            raise RuntimeError(f"crash/reset output while listing faces: {line}")
        m = FACE_RE.match(line)
        if m:
            faces.append({"tour": int(m.group(1)), "name": m.group(2).strip(), "enum": int(m.group(3))})
    if not faces:
        raise RuntimeError("no faces returned by `qa faces`")
    return faces


def capture_screen64(ser: serial.Serial, dest: Path, timeout_s: float) -> tuple[bool, str]:
    ser.reset_input_buffer()
    send_line(ser, "qa screen64")
    started = False
    expected: int | None = None
    chunks: list[str] = []
    noise = 0
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        raw = ser.readline()
        if not raw:
            continue
        line = raw.decode("ascii", errors="ignore").strip()
        if CRASH_RE.search(line):
            raise RuntimeError(f"crash/reset output during screenshot: {line}")
        if not started:
            m = re.match(r"qa: screen64 begin len=(\d+)", line)
            if m:
                expected = int(m.group(1))
                started = True
            elif line.startswith("qa: screen64 error"):
                return False, line
            continue
        if line == "qa: screen64 end":
            payload = "".join(chunks)
            data = base64.b64decode(payload, validate=True)
            dest.write_bytes(data)
            if data[:2] != b"BM":
                return False, f"not a BMP ({len(data)} bytes)"
            if expected is not None and len(data) != expected:
                return False, f"size mismatch got={len(data)} expected={expected}"
            return True, f"{len(data)}B noise={noise}"
        if line.startswith("qa: screen64 error"):
            return False, line
        if line and B64_RE.match(line) and len(line) % 4 == 0:
            chunks.append(line)
        elif line:
            noise += 1
    return False, "timeout waiting for screen64 end"


def make_contact_sheet(paths: list[Path], out: Path) -> None:
    try:
        from PIL import Image, ImageDraw
    except ModuleNotFoundError:
        return
    if not paths:
        return
    thumb = 150
    label_h = 28
    cols = 5
    rows = (len(paths) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * thumb, rows * (thumb + label_h)), (18, 18, 18))
    draw = ImageDraw.Draw(sheet)
    for idx, path in enumerate(paths):
        try:
            im = Image.open(path).convert("RGB")
            im.thumbnail((thumb, thumb))
            x = (idx % cols) * thumb + (thumb - im.width) // 2
            y = (idx // cols) * (thumb + label_h) + (thumb - im.height) // 2
            sheet.paste(im, (x, y))
            draw.text(((idx % cols) * thumb + 4, (idx // cols) * (thumb + label_h) + thumb + 5),
                      path.stem[:20], fill=(235, 235, 235))
        except Exception as exc:  # noqa: BLE001 - keep QA moving and report visible failure.
            draw.text(((idx % cols) * thumb + 4, (idx // cols) * (thumb + label_h) + 20),
                      f"{path.name}\n{exc}", fill=(255, 90, 90))
    out.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(out)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="/dev/cu.usbmodem1101")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--paint-sec", type=float, default=1.4)
    parser.add_argument("--timeout-sec", type=float, default=90.0)
    parser.add_argument("--out-dir", default="")
    parser.add_argument("--start", type=int, default=0, help="first tour index to capture")
    parser.add_argument("--end", type=int, default=999, help="last tour index to capture")
    parser.add_argument("--only", default="", help="comma-separated tour indices to capture")
    parser.add_argument("--retries", type=int, default=1)
    args = parser.parse_args()

    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    out_dir = Path(args.out_dir) if args.out_dir else ROOT / "artifacts" / "qa" / f"serial-screens-{stamp}"
    out_dir.mkdir(parents=True, exist_ok=True)
    print(f"serial_screenshot_tour: port={args.port} out={out_dir}", flush=True)

    ser = serial.Serial(args.port, args.baud, timeout=0.25)
    try:
      time.sleep(0.4)
      faces = get_faces(ser)
      captured: list[Path] = []
      failures: list[str] = []
      only = {int(x) for x in args.only.split(",") if x.strip()} if args.only else None
      for face in faces:
          if only is not None and face["tour"] not in only:
              continue
          if only is None and not (args.start <= face["tour"] <= args.end):
              continue
          name = re.sub(r"[^a-z0-9_-]+", "_", face["name"].lower()).strip("_")
          dest = out_dir / f"{face['tour']:02d}-{name}.bmp"
          send_line(ser, f"face {face['enum']}")
          lines = read_lines(ser, args.paint_sec)
          for line in lines:
              if line and (line.startswith("face:") or CRASH_RE.search(line)):
                  print(line, flush=True)
              if CRASH_RE.search(line):
                  raise RuntimeError(f"crash/reset output selecting {face['name']}: {line}")
          ok = False
          detail = ""
          for attempt in range(args.retries + 1):
              ok, detail = capture_screen64(ser, dest, args.timeout_sec)
              if ok:
                  break
              if dest.exists():
                  dest.unlink()
              time.sleep(0.5)
              if attempt < args.retries:
                  print(f"retry {face['tour']:02d} {face['name']} after {detail}", flush=True)
          print(f"capture {face['tour']:02d} {face['name']} ok={ok} {detail}", flush=True)
          if ok:
              captured.append(dest)
          else:
              failures.append(f"{face['tour']:02d} {face['name']}: {detail}")
      make_contact_sheet(captured, out_dir / "contact-sheet.png")
      if failures:
          (out_dir / "failures.txt").write_text("\n".join(failures) + "\n", encoding="utf-8")
          return 2
      return 0
    finally:
      ser.close()


if __name__ == "__main__":
    raise SystemExit(main())
