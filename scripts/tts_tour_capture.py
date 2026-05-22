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
STATUS_RE = re.compile(r"qa: face=.*?\bwifi=(\d+)\s+time=(\d+)\s+ip=([0-9.]+)")
SUMMARY_RE = re.compile(r"tour: summary tts_ok=(\d+) tts_fail=(\d+) tts_skip=(\d+)")
TTS_OK_RE = re.compile(r"tour: tts ok\s+(\d+)")
TTS_FAIL_RE = re.compile(r"tour: (?:narrate failed|narrate timeout|narrate no audio|tts speaker failed)\s+(\d+)")
TTS_SKIP_RE = re.compile(r"tour: tts skipped\s+(\d+)\s+([a-z0-9_ -]+).*?reason=(.*)", re.I)
TTS_START_RE = re.compile(r"tour: tts\s+(\d+)\s+([a-z0-9_ -]+)", re.I)
HTTP_POST_RE = re.compile(r"voice: HTTP POST message \((\d+) B body(, retry)?\)")
HTTP_CODE_RE = re.compile(r"voice: HTTP (-?\d+) \(message\)")
MP3_LEN_RE = re.compile(r"voice: MP3 Content-Length\s+(\d+)")
MP3_RECV_RE = re.compile(r"voice: MP3 recv\s+(\d+) B")
MP3_COMPLETE_RE = re.compile(r"voice: MP3 complete\s+(\d+) B")
MESSAGE_OK_RE = re.compile(r"voice: message ok mp3=(\d+) B")


def ms_between(start: float | None, end: float | None) -> float | None:
    if start is None or end is None:
        return None
    return round((end - start) * 1000, 1)


def mark_timing(face: dict, key: str, ts: float) -> None:
    face.setdefault("timing", {})[key] = round(ts, 3)


def build_timing_summary(faces: list[dict], tour_start_ts: float | None, tour_done_ts: float | None) -> dict:
    rows = []
    for idx, face in enumerate(faces):
        timing = face.get("timing", {})
        next_load = faces[idx + 1].get("timing", {}).get("load_ts") if idx + 1 < len(faces) else tour_done_ts
        outcome_ts = timing.get("tts_ok_ts") or timing.get("tts_fail_ts") or timing.get("tts_skip_ts")
        row = {
            "id": face.get("id"),
            "name": face.get("name"),
            "outcome": timing.get("outcome"),
            "load_to_tts_ms": ms_between(timing.get("load_ts"), timing.get("tts_start_ts")),
            "tts_total_ms": ms_between(timing.get("tts_start_ts"), outcome_ts),
            "http_post_to_200_ms": ms_between(timing.get("http_post_ts"), timing.get("http_200_ts")),
            "http_post_to_fail_ms": ms_between(timing.get("http_post_ts"), timing.get("http_fail_ts")),
            "mp3_download_ms": ms_between(timing.get("mp3_read_ts") or timing.get("http_200_ts"), timing.get("mp3_complete_ts")),
            "playback_ms": ms_between(timing.get("mp3_complete_ts") or timing.get("message_ok_ts"), timing.get("tts_ok_ts")),
            "face_total_ms": ms_between(timing.get("load_ts"), next_load),
            "mp3_len": timing.get("mp3_len"),
            "http_body_bytes": timing.get("http_body_bytes"),
            "http_retries": timing.get("http_retries", 0),
            "failure": timing.get("failure"),
            "skip_reason": timing.get("skip_reason"),
        }
        rows.append(row)

    ok_rows = [r for r in rows if r["outcome"] == "ok"]
    fail_rows = [r for r in rows if r["outcome"] == "fail"]

    def average(key: str, subset: list[dict]) -> float | None:
        vals = [r[key] for r in subset if isinstance(r.get(key), (int, float))]
        return round(sum(vals) / len(vals), 1) if vals else None

    return {
        "tour_duration_ms": ms_between(tour_start_ts, tour_done_ts),
        "ok_avg_tts_total_ms": average("tts_total_ms", ok_rows),
        "ok_avg_http_post_to_200_ms": average("http_post_to_200_ms", ok_rows),
        "ok_avg_mp3_download_ms": average("mp3_download_ms", ok_rows),
        "ok_avg_playback_ms": average("playback_ms", ok_rows),
        "fail_avg_tts_total_ms": average("tts_total_ms", fail_rows),
        "faces": rows,
    }


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
    parser.add_argument("--wait-ready-sec", type=int, default=0, help="Wait for qa status wifi=1 time=1 before starting")
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
    active_face_id: int | None = None
    tour_start_ts: float | None = None
    tour_done_ts: float | None = None
    ready = False

    try:
        time.sleep(0.4)
        ser.reset_input_buffer()
        wait_deadline = time.monotonic() + (args.wait_ready_sec if args.wait_ready_sec > 0 else 18)
        while time.monotonic() < wait_deadline:
            ser.write(b"qa status\n")
            ser.flush()
            for line in read_lines(ser, 1.0):
                print(line, flush=True)
                statuses.append(line)
                m = STATUS_RE.search(line)
                if m:
                    wifi = m.group(1) == "1"
                    valid_time = m.group(2) == "1"
                    ip = m.group(3)
                    ready = wifi and valid_time and ip != "0.0.0.0"
            if ready or (args.wait_ready_sec <= 0 and ip and ip != "0.0.0.0"):
                break
            time.sleep(1.5)
        if args.wait_ready_sec > 0 and not ready:
            print(f"tts_tour_capture: device not ready after {args.wait_ready_sec}s; starting anyway", flush=True)

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
                    line_ts = time.time()
                    log.write(f"{line_ts:.3f} {line}\n")
                    log.flush()
                    print(line, flush=True)

                    if line.startswith("tour: start"):
                        tour_start_ts = line_ts
                    if CRASH_RE.search(line):
                        crashes.append(line)
                    if "[E]" in line or "HTTP -1" in line or "failed" in line or "fail" in line.lower():
                        errors.append(line)
                    if line.startswith("qa: face="):
                        statuses.append(line)
                        m = STATUS_RE.search(line)
                        if m:
                            ip = m.group(3)

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
                        mark_timing(faces[face_id], "load_ts", line_ts)
                        ser.write(b"qa status\n")
                        ser.flush()

                    m = TTS_START_RE.search(line)
                    if m:
                        active_face_id = int(m.group(1))
                        face = faces.setdefault(active_face_id, {"id": active_face_id, "name": m.group(2).strip().replace(" ", "_")})
                        mark_timing(face, "tts_start_ts", line_ts)
                    m = TTS_SKIP_RE.search(line)
                    if m:
                        face_id = int(m.group(1))
                        face = faces.setdefault(face_id, {"id": face_id, "name": m.group(2).strip().replace(" ", "_")})
                        mark_timing(face, "tts_skip_ts", line_ts)
                        face.setdefault("timing", {})["outcome"] = "skip"
                        face.setdefault("timing", {})["skip_reason"] = m.group(3).strip()
                    if active_face_id is not None:
                        face = faces.setdefault(active_face_id, {"id": active_face_id, "name": str(active_face_id)})
                        timing = face.setdefault("timing", {})
                        m = HTTP_POST_RE.search(line)
                        if m:
                            if "http_post_ts" not in timing:
                                mark_timing(face, "http_post_ts", line_ts)
                                timing["http_body_bytes"] = int(m.group(1))
                            else:
                                mark_timing(face, "last_http_post_ts", line_ts)
                            if m.group(2):
                                timing["http_retries"] = timing.get("http_retries", 0) + 1
                        m = HTTP_CODE_RE.search(line)
                        if m:
                            code = int(m.group(1))
                            if code == 200:
                                mark_timing(face, "http_200_ts", line_ts)
                            else:
                                mark_timing(face, "http_fail_ts", line_ts)
                                timing["http_code"] = code
                        if line == "voice: reading MP3 body…":
                            mark_timing(face, "mp3_read_ts", line_ts)
                        m = MP3_LEN_RE.search(line)
                        if m:
                            timing["mp3_len"] = int(m.group(1))
                        m = MP3_RECV_RE.search(line)
                        if m:
                            timing["last_mp3_recv_bytes"] = int(m.group(1))
                            mark_timing(face, "last_mp3_recv_ts", line_ts)
                        m = MP3_COMPLETE_RE.search(line)
                        if m:
                            timing["mp3_complete_bytes"] = int(m.group(1))
                            mark_timing(face, "mp3_complete_ts", line_ts)
                        m = MESSAGE_OK_RE.search(line)
                        if m:
                            timing["message_ok_bytes"] = int(m.group(1))
                            mark_timing(face, "message_ok_ts", line_ts)

                    m = TTS_OK_RE.search(line)
                    if m:
                        face_id = int(m.group(1))
                        tts_ok.add(face_id)
                        face = faces.setdefault(face_id, {"id": face_id, "name": str(face_id)})
                        mark_timing(face, "tts_ok_ts", line_ts)
                        face.setdefault("timing", {})["outcome"] = "ok"
                        if ip:
                            name = str(face.get("name", face_id)).replace(" ", "_")
                            bmp = out_dir / f"{face_id:02d}-{name}.bmp"
                            fut = executor.submit(fetch_screenshot, ip, bmp)
                            captures.append((face_id, name, fut))
                            fut.result()
                        active_face_id = None
                    m = TTS_FAIL_RE.search(line)
                    if m:
                        face_id = int(m.group(1))
                        tts_fail.add(face_id)
                        face = faces.setdefault(face_id, {"id": face_id, "name": str(face_id)})
                        mark_timing(face, "tts_fail_ts", line_ts)
                        face.setdefault("timing", {})["outcome"] = "fail"
                        face.setdefault("timing", {})["failure"] = line
                        active_face_id = None
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
                        tour_done_ts = line_ts
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
    ordered_faces = [faces[k] for k in sorted(faces)]
    timing_summary = build_timing_summary(ordered_faces, tour_start_ts, tour_done_ts)
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
        "timing": timing_summary,
        "faces": ordered_faces,
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
