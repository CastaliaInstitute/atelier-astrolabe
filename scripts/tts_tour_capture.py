#!/usr/bin/env python3
"""Run `qa tour tts` on hardware and capture serial + screen.bmp evidence."""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
import time
import urllib.request
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]

try:
    import serial
except ModuleNotFoundError:
    venv_python = ROOT / "mcp" / "astrolabe-esp" / ".venv" / "bin" / "python"
    if venv_python.exists() and Path(sys.executable) != venv_python:
        os.execv(str(venv_python), [str(venv_python), *sys.argv])
    raise

CRASH_RE = re.compile(r"Guru Meditation|Backtrace:|Stack canary|stack overflow|panic|Brownout|ASTROLABE_ALERT", re.I)
LOAD_RE = re.compile(r"tour: loading\s+(\d+)\s+([a-z0-9_ -]+).*?heap=(\d+).*?largest=(\d+).*?psram=(\d+)", re.I)
STATUS_RE = re.compile(r"qa: face=.*?\bip=([0-9.]+)")
SUMMARY_RE = re.compile(r"tour: summary tts_ok=(\d+) tts_fail=(\d+) tts_skip=(\d+)")
TTS_OK_RE = re.compile(r"tour: tts ok\s+(\d+)")
TTS_FAIL_RE = re.compile(r"tour: (?:narrate failed|narrate timeout|narrate no audio|tts speaker failed)\s+(\d+)")


def detect_port() -> str:
    env_port = os.environ.get("ASTROLABE_SERIAL_PORT") or os.environ.get("ASTROLABE_UPLOAD_PORT")
    if env_port:
        return env_port
    proc = subprocess.run([str(ROOT / "scripts" / "detect_upload_port.sh")], text=True, capture_output=True, check=True)
    return proc.stdout.strip()


def fetch_screenshot(ip: str, dest: Path) -> dict:
    started = time.perf_counter()
    try:
        with urllib.request.urlopen(f"http://{ip}/screen.bmp", timeout=18) as resp:
            data = resp.read()
        dest.write_bytes(data)
        detail = f"{len(data)}B"
        ok = len(data) > 1024 and data[:2] == b"BM"
        if ok and len(data) > 2048:
            sample = data[1024:: max(1, len(data) // 4096)]
            if len(set(sample)) <= 2:
                ok = False
                detail += " blank-looking"
        return {
            "ok": ok,
            "detail": detail,
            "path": str(dest),
            "elapsed_ms": round((time.perf_counter() - started) * 1000, 1),
        }
    except Exception as exc:  # noqa: BLE001 - artifact should capture the concrete failure text.
        return {
            "ok": False,
            "detail": f"{type(exc).__name__}: {exc}",
            "path": str(dest),
            "elapsed_ms": round((time.perf_counter() - started) * 1000, 1),
        }


def read_lines(ser: serial.Serial, seconds: float) -> list[str]:
    deadline = time.monotonic() + seconds
    buf = ""
    lines: list[str] = []
    while time.monotonic() < deadline:
        chunk = ser.read(4096)
        if not chunk:
            time.sleep(0.03)
            continue
        buf += chunk.decode("utf-8", errors="replace")
        while "\n" in buf:
            line, buf = buf.split("\n", 1)
            lines.append(line.rstrip("\r"))
    if buf:
        lines.append(buf.rstrip("\r"))
    return lines


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="")
    parser.add_argument("--dwell-ms", type=int, default=1200)
    parser.add_argument("--timeout-sec", type=int, default=900)
    parser.add_argument("--out-dir", default="")
    args = parser.parse_args()

    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    out_dir = Path(args.out_dir) if args.out_dir else ROOT / "artifacts" / "qa" / f"tts-tour-{stamp}"
    out_dir.mkdir(parents=True, exist_ok=True)
    serial_log = out_dir / "serial-timestamped.log"
    summary_path = out_dir / "summary.json"
    port = args.port or detect_port()

    print(f"tts_tour_capture: port={port} out={out_dir}", flush=True)
    ser = serial.Serial(port, 115200, timeout=0.25)
    executor = ThreadPoolExecutor(max_workers=3)
    captures = []
    faces: dict[int, dict] = {}
    seen: set[int] = set()
    tts_ok: set[int] = set()
    tts_fail: set[int] = set()
    statuses: list[str] = []
    crashes: list[str] = []
    errors: list[str] = []
    final_marker = ""
    tour_summary = None
    ip = ""

    try:
        time.sleep(0.4)
        ser.reset_input_buffer()
        for _ in range(12):
            ser.write(b"qa status\n")
            ser.flush()
            for line in read_lines(ser, 1.0):
                print(line, flush=True)
                statuses.append(line)
                m = STATUS_RE.search(line)
                if m:
                    ip = m.group(1)
            if ip and ip != "0.0.0.0":
                break
            time.sleep(1.5)

        command = f"qa tour tts {args.dwell_ms}"
        ser.write((command + "\n").encode("utf-8"))
        ser.flush()

        started = time.monotonic()
        buf = ""
        with serial_log.open("w", encoding="utf-8") as log:
            log.write(f"# command={command}\n# port={port}\n# ip={ip}\n")
            while time.monotonic() - started < args.timeout_sec:
                chunk = ser.read(4096)
                if not chunk:
                    time.sleep(0.03)
                    continue
                text = chunk.decode("utf-8", errors="replace")
                buf += text
                while "\n" in buf:
                    line, buf = buf.split("\n", 1)
                    line = line.rstrip("\r")
                    log.write(f"{time.time():.3f} {line}\n")
                    log.flush()
                    print(line, flush=True)

                    if CRASH_RE.search(line):
                        crashes.append(line)
                    if "[E]" in line or "HTTP -1" in line or "failed" in line or "fail" in line.lower():
                        errors.append(line)
                    if line.startswith("qa: face="):
                        statuses.append(line)
                        m = STATUS_RE.search(line)
                        if m:
                            ip = m.group(1)

                    m = LOAD_RE.search(line)
                    if m:
                        face_id = int(m.group(1))
                        name = m.group(2).strip().replace(" ", "_")
                        seen.add(face_id)
                        faces[face_id] = {
                            "id": face_id,
                            "name": name,
                            "heap": int(m.group(3)),
                            "largest": int(m.group(4)),
                            "psram": int(m.group(5)),
                        }
                        ser.write(b"qa status\n")
                        ser.flush()
                        if ip:
                            bmp = out_dir / f"{face_id:02d}-{name}.bmp"
                            captures.append((face_id, name, executor.submit(fetch_screenshot, ip, bmp)))

                    m = TTS_OK_RE.search(line)
                    if m:
                        tts_ok.add(int(m.group(1)))
                    m = TTS_FAIL_RE.search(line)
                    if m:
                        tts_fail.add(int(m.group(1)))
                    m = SUMMARY_RE.search(line)
                    if m:
                        tour_summary = {
                            "tts_ok": int(m.group(1)),
                            "tts_fail": int(m.group(2)),
                            "tts_skip": int(m.group(3)),
                        }
                    if line in ("TTS_TOUR PASS", "TTS_TOUR FAIL"):
                        final_marker = line
                    if line == "tour: done":
                        break
                else:
                    continue
                break
    finally:
        ser.close()

    screenshots = []
    for face_id, name, fut in captures:
        shot = {"id": face_id, "name": name, **fut.result()}
        screenshots.append(shot)
        faces.setdefault(face_id, {"id": face_id, "name": name})["screenshot"] = shot
    executor.shutdown(wait=True)

    result = "done" if final_marker else "timeout"
    summary = {
        "timestamp": stamp,
        "port": port,
        "ip": ip,
        "command": f"qa tour tts {args.dwell_ms}",
        "result": result,
        "final_marker": final_marker,
        "tour_summary": tour_summary,
        "seen_count": len(seen),
        "seen": sorted(seen),
        "tts_ok_count": len(tts_ok),
        "tts_fail_count": len(tts_fail),
        "screenshot_ok_count": sum(1 for s in screenshots if s["ok"]),
        "screenshot_count": len(screenshots),
        "crash_count": len(crashes),
        "error_count": len(errors),
        "faces": [faces[k] for k in sorted(faces)],
        "screenshots": screenshots,
        "statuses": statuses,
        "crashes": crashes,
        "errors_tail": errors[-40:],
        "serial_log": str(serial_log),
    }
    summary_path.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(f"tts_tour_capture: summary={summary_path}", flush=True)
    if crashes:
        return 2
    if result != "done":
        return 3
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
