#!/usr/bin/env python3
"""
Hardware functional tests: every clock face → screenshot + injected touch/button → crash check.

Requires Waveshare 1.75C on USB, Wi‑Fi secrets, and firmware with `qa` serial commands.

  ./scripts/functional_test.py --flash
  ./scripts/functional_test.py --faces classic,moon
  ./scripts/functional_test.py --faces castalia   # face 8 → swipe left → assert face 9 (synastry)
  ./scripts/test_castalia_swipe.py --flash        # thin wrapper for the castalia matrix row
"""
from __future__ import annotations

import argparse
import json
import re
import struct
import subprocess
import sys
import time
import urllib.error
import urllib.request
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
DEFAULT_MATRIX = REPO / "tests" / "functional" / "faces_astrolabe.json"
OUT_DIR = REPO / "artifacts" / "functional"

CRASH_PATTERNS = [
    re.compile(r"Guru Meditation", re.I),
    re.compile(r"abort\(\)", re.I),
    re.compile(r"Backtrace:", re.I),
    re.compile(r"Stack overflow", re.I),
    re.compile(r"panic'ed", re.I),
    re.compile(r"Brownout detector", re.I),
    re.compile(r"rst:0x[0-9a-f]+.*(?:panic|WDT|BROWNOUT)", re.I),
]


@dataclass
class StepResult:
    name: str
    ok: bool
    detail: str = ""


@dataclass
class FaceResult:
    face_id: int
    name: str
    ok: bool
    steps: list[StepResult] = field(default_factory=list)
    screenshot: str = ""
    serial_log: str = ""
    serial_lines: list[str] = field(default_factory=list)


class WatchSerial:
    def __init__(self, port: str, baud: int = 115200) -> None:
        import serial

        self._ser = serial.Serial(port, baud, timeout=0.25)
        self._buf = ""
        time.sleep(0.15)
        self._ser.setDTR(False)
        self._ser.setRTS(False)
        time.sleep(0.05)
        self._ser.setDTR(True)
        self._ser.setRTS(True)
        time.sleep(0.05)
        self._ser.reset_input_buffer()

    def close(self) -> None:
        self._ser.close()

    def _drain(self, timeout: float = 0.0) -> list[str]:
        lines: list[str] = []
        deadline = time.time() + timeout
        while time.time() < deadline:
            chunk = self._ser.read(4096)
            if chunk:
                self._buf += chunk.decode("utf-8", errors="replace")
                while "\n" in self._buf:
                    line, self._buf = self._buf.split("\n", 1)
                    line = line.rstrip("\r")
                    if line:
                        lines.append(line)
            elif timeout > 0:
                time.sleep(0.05)
            else:
                break
        return lines

    def send(self, cmd: str, wait: float = 0.35) -> list[str]:
        self._ser.write((cmd.strip() + "\n").encode("utf-8"))
        self._ser.flush()
        return self._drain(wait)

    def wait_line(self, pattern: re.Pattern[str], timeout: float = 30.0) -> str | None:
        deadline = time.time() + timeout
        while time.time() < deadline:
            for ln in self._drain(0.15):
                if pattern.search(ln):
                    return ln
        return None

    def collect(self, seconds: float) -> list[str]:
        return self._drain(seconds)

    def check_crashes(self, lines: list[str]) -> str | None:
        for ln in lines:
            for pat in CRASH_PATTERNS:
                if pat.search(ln):
                    return ln
        return None


def detect_port() -> str:
    script = REPO / "scripts" / "detect_upload_port.sh"
    out = subprocess.check_output([str(script)], text=True).strip()
    if not out:
        raise RuntimeError("no upload port — plug in watch")
    return out


def ensure_secrets() -> None:
    local = REPO / "include" / "secrets.local.h"
    if local.is_file():
        return
    alt = Path.home() / "GitHub" / "astrolabe" / "include" / "secrets.local.h"
    if alt.is_file():
        local.write_bytes(alt.read_bytes())
        return
    castalia = Path.home() / "GitHub" / "CastaliaInstitute" / "astrolabe" / "include" / "secrets.local.h"
    if castalia.is_file():
        local.write_bytes(castalia.read_bytes())
        return
    env = {
        "MYNAH_WIFI_SSID": __import__("os").environ.get("MYNAH_WIFI_SSID", ""),
        "MYNAH_WIFI_PASSWORD": __import__("os").environ.get("MYNAH_WIFI_PASSWORD", ""),
        "MYNAH_SUPABASE_URL": __import__("os").environ.get("MYNAH_SUPABASE_URL", ""),
        "MYNAH_SUPABASE_ANON_KEY": __import__("os").environ.get("MYNAH_SUPABASE_ANON_KEY", ""),
    }
    if env["MYNAH_WIFI_SSID"]:
        subprocess.check_call([str(REPO / "scripts" / "write_secrets_ci.sh")], env={**__import__("os").environ, **env})
        return
    raise RuntimeError("missing include/secrets.local.h or MYNAH_WIFI_* env")


def flash_firmware(env: str, port: str) -> None:
    subprocess.check_call(
        [str(REPO / "scripts" / "build.sh"), "-t", "upload", "--upload-port", port],
        env={**__import__("os").environ, "PIO_ENV": env, "ASTROLABE_UPLOAD_PORT": port},
        cwd=str(REPO),
    )


def wait_wifi_ip(ser: WatchSerial, timeout: float = 90.0) -> str:
    ip_re = re.compile(r"(?:Screen over WiFi|Settings: http)://(\d+\.\d+\.\d+\.\d+)")
    deadline = time.time() + timeout
    while time.time() < deadline:
        for ln in ser.collect(2.0):
            m = ip_re.search(ln)
            if m:
                return m.group(1)
    raise RuntimeError("watch did not join Wi‑Fi / print screen URL")


def validate_bmp(path: Path, min_bytes: int = 50000) -> tuple[bool, str]:
    if not path.is_file() or path.stat().st_size < 54:
        return False, "missing or tiny BMP"
    data = path.read_bytes()
    if data[:2] != b"BM":
        return False, "not BMP"
    if len(data) < 26:
        return False, "truncated header"
    w = struct.unpack_from("<i", data, 18)[0]
    h = struct.unpack_from("<i", data, 22)[0]
    aw, ah = abs(w), abs(h)
    if aw < 400 or ah < 400:
        return False, f"unexpected size {aw}x{ah}"
    if path.stat().st_size < min_bytes:
        return False, f"file only {path.stat().st_size} bytes"
    pix = data[54 : 54 + min(8000, len(data) - 54)]
    if len(set(pix)) < 4:
        return False, "frame looks blank"
    return True, f"{aw}x{ah}"


def fetch_screenshot(ip: str, dest: Path) -> None:
    dest.parent.mkdir(parents=True, exist_ok=True)
    with urllib.request.urlopen(f"http://{ip}/screen.bmp", timeout=15) as resp:
        dest.write_bytes(resp.read())


def gesture_to_qa(g: str) -> str:
    if g.startswith("tap:"):
        xy = g.split(":", 1)[1]
        x, y = xy.split(",")
        return f"qa inject tap {x} {y}"
    if g.startswith("swipe_"):
        direction = g.replace("swipe_", "")
        return f"qa inject swipe {direction}"
    raise ValueError(f"unknown gesture {g}")


def button_to_qa(b: str) -> str:
    if b == "boot":
        return "qa inject boot"
    if b == "pwr":
        return "qa inject pwr"
    if b == "pwr_hold":
        return "qa inject pwr hold"
    raise ValueError(f"unknown button {b}")


def benchmark_face_loads(
    ser: WatchSerial,
    ip: str,
    faces: list[dict],
    out_path: Path,
    paint_sec: float,
) -> list[dict]:
    """Measure serial ack and optional screen.bmp latency per face."""
    rows: list[dict] = []
    face_ack = re.compile(r"face:\s*(\d+)\b")
    for spec in faces:
        fid = int(spec["id"])
        name = spec["name"]
        t0 = time.perf_counter()
        ser.send(f"face {fid}", wait=0.05)
        ack_ms: float | None = None
        deadline = time.perf_counter() + 15.0
        while time.perf_counter() < deadline:
            for ln in ser._drain(0.12):
                m = face_ack.search(ln)
                if m and int(m.group(1)) == fid:
                    ack_ms = (time.perf_counter() - t0) * 1000.0
                    break
            if ack_ms is not None:
                break
        time.sleep(float(spec.get("paint_sec", paint_sec)))
        screenshot_ms: float | None = None
        screenshot_ok = False
        if ip:
            bmp = out_path.parent / f"bench-{fid:02d}-{name}.bmp"
            t1 = time.perf_counter()
            try:
                fetch_screenshot(ip, bmp)
                screenshot_ms = (time.perf_counter() - t1) * 1000.0
                screenshot_ok, _ = validate_bmp(bmp)
            except (urllib.error.URLError, TimeoutError, OSError):
                screenshot_ms = (time.perf_counter() - t1) * 1000.0
        rows.append(
            {
                "face_id": fid,
                "name": name,
                "ack_ms": round(ack_ms, 1) if ack_ms is not None else None,
                "screenshot_ms": round(screenshot_ms, 1) if screenshot_ms is not None else None,
                "screenshot_ok": screenshot_ok,
            }
        )
        print(
            f"  face {fid} {name}: ack={rows[-1]['ack_ms']}ms"
            + (
                f" bmp={rows[-1]['screenshot_ms']}ms"
                if rows[-1]["screenshot_ms"] is not None
                else " bmp=n/a"
            )
        )
    payload = {
        "timestamp": out_path.stem.replace("face-load-times-", ""),
        "ip": ip,
        "faces": rows,
    }
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(payload, indent=2), encoding="utf-8")
    print(f"→ face load benchmark {out_path}")
    return rows


def run_face(
    ser: WatchSerial,
    ip: str,
    spec: dict,
    out_dir: Path,
    paint_sec: float,
    step_pause: float,
) -> FaceResult:
    fid = int(spec["id"])
    name = spec["name"]
    result = FaceResult(face_id=fid, name=name, ok=True)
    face_log: list[str] = []

    def absorb(lines: list[str]) -> None:
        face_log.extend(lines)

    def step(name: str, ok: bool, detail: str = "") -> None:
        result.steps.append(StepResult(name, ok, detail))
        if not ok:
            result.ok = False

    lines = ser.send(f"face {fid}", wait=1.0)
    absorb(lines)
    if not ser.wait_line(re.compile(rf"face:\s*{fid}\b"), timeout=8.0):
        step("set_face", False, "no face: ack on serial")
        return result
    step("set_face", True)

    face_paint = float(spec.get("paint_sec", paint_sec))
    time.sleep(face_paint)
    crash = ser.check_crashes(lines)
    if crash:
        step("after_set_face", False, crash)
        return result

    bmp = out_dir / f"{fid:02d}-{name}.bmp"
    try:
        fetch_screenshot(ip, bmp)
        ok, detail = validate_bmp(bmp)
        step("screenshot", ok, detail)
        if ok:
            result.screenshot = str(bmp)
    except (urllib.error.URLError, TimeoutError, OSError) as e:
        step("screenshot", False, str(e))

    for g in spec.get("gestures", []):
        cmd = gesture_to_qa(g)
        glines = ser.send(cmd, wait=step_pause)
        absorb(glines)
        err = ser.check_crashes(glines)
        step(f"gesture:{g}", err is None, err or "ok")

    for b in spec.get("buttons", []):
        cmd = button_to_qa(b)
        blines = ser.send(cmd, wait=step_pause)
        absorb(blines)
        err = ser.check_crashes(blines)
        step(f"button:{b}", err is None, err or "ok")
        if b == "boot":
            time.sleep(0.5)
            absorb(ser.send("face " + str(fid), wait=0.8))

    status_lines = ser.send("qa status", wait=0.4)
    absorb(status_lines)
    err = ser.check_crashes(status_lines)
    m = None
    for ln in status_lines:
        if ln.startswith("qa: face="):
            m = ln
    if err:
        step("qa_status", False, err)
    elif not m:
        step("qa_status", False, "no qa: face= line")
    else:
        expected = spec.get("expect_face")
        if expected is not None:
            fm = re.search(r"face=(\d+)", m)
            actual = int(fm.group(1)) if fm else -1
            ok = actual == int(expected)
            detail = m if ok else f"{m} (expected face {expected})"
            step("qa_status", ok, detail)
        else:
            step("qa_status", True, m)

    log_path = out_dir / f"{fid:02d}-{name}-serial.log"
    log_path.write_text("\n".join(face_log) + "\n", encoding="utf-8")
    result.serial_log = str(log_path)
    result.serial_lines = face_log
    return result


def load_matrix(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def main() -> int:
    parser = argparse.ArgumentParser(description="Astrolabe face functional tests (hardware)")
    parser.add_argument("--matrix", type=Path, default=DEFAULT_MATRIX)
    parser.add_argument("--faces", default="", help="comma names or ids; default all")
    parser.add_argument("--flash", action="store_true", help="build + upload before tests")
    parser.add_argument("--env", default="waveshare_s3_175")
    parser.add_argument("--port", default="")
    parser.add_argument("--ip", default="", help="skip Wi‑Fi wait; use known watch IP")
    parser.add_argument("--paint-sec", type=float, default=2.5)
    parser.add_argument("--step-pause", type=float, default=0.65)
    parser.add_argument("--wifi-timeout", type=float, default=90.0)
    parser.add_argument("--output", type=Path, default=OUT_DIR)
    parser.add_argument(
        "--remediate",
        action="store_true",
        help="on failure run triage + issue + unmerge (see functional_remediate.sh)",
    )
    parser.add_argument(
        "--benchmark-load",
        action="store_true",
        help="measure per-face serial ack + screen.bmp time; writes face-load-times-*.json",
    )
    args = parser.parse_args()

    matrix = load_matrix(args.matrix)
    port = args.port.strip() or detect_port()
    ensure_secrets()

    stamp = datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%S")
    out_dir = args.output / stamp
    out_dir.mkdir(parents=True, exist_ok=True)

    if args.flash:
        print(f"→ flash {args.env} to {port}")
        flash_firmware(args.env, port)
        time.sleep(6.0)

    ser = WatchSerial(port)
    try:
        ip = args.ip.strip()
        if ip:
            print(f"→ watch at {ip} (--ip)")
        else:
            print("→ waiting for Wi‑Fi + screen server…")
            ip = wait_wifi_ip(ser, timeout=args.wifi_timeout)
            print(f"→ watch at {ip}")

        faces = matrix["faces"]
        if args.faces.strip():
            want = {x.strip().lower() for x in args.faces.split(",")}
            faces = [f for f in faces if f["name"] in want or str(f["id"]) in want]

        if args.benchmark_load:
            bench_faces = sorted(matrix["faces"], key=lambda f: int(f["id"]))
            if args.faces.strip():
                bench_faces = faces
            bench_path = OUT_DIR / f"face-load-times-{stamp}.json"
            benchmark_face_loads(ser, ip, bench_faces, bench_path, args.paint_sec)

        results: list[FaceResult] = []
        for spec in faces:
            print(f"→ face {spec['id']} {spec['name']}")
            fr = run_face(ser, ip, spec, out_dir, args.paint_sec, args.step_pause)
            results.append(fr)
            mark = "PASS" if fr.ok else "FAIL"
            print(f"  {mark}")

        face_specs = {int(f["id"]): f for f in matrix["faces"]}
        try:
            git_head = subprocess.check_output(
                ["git", "rev-parse", "HEAD"], cwd=str(REPO), text=True
            ).strip()
        except subprocess.CalledProcessError:
            git_head = ""

        report = {
            "timestamp": stamp,
            "gate": "hardware",
            "matrix": str(args.matrix),
            "ip": ip,
            "port": port,
            "git_head": git_head,
            "passed": sum(1 for r in results if r.ok),
            "failed": sum(1 for r in results if not r.ok),
            "faces": [
                {
                    "id": r.face_id,
                    "name": r.name,
                    "ok": r.ok,
                    "screenshot": r.screenshot,
                    "serial_log": r.serial_log,
                    "source_paths": face_specs.get(r.face_id, {}).get("source_paths", []),
                    "steps": [{"name": s.name, "ok": s.ok, "detail": s.detail} for s in r.steps],
                }
                for r in results
            ],
        }
        report_path = out_dir / "report.json"
        report_path.write_text(json.dumps(report, indent=2), encoding="utf-8")
        latest = OUT_DIR / "latest"
        latest.mkdir(parents=True, exist_ok=True)
        (latest / "report.json").write_text(report_path.read_text(encoding="utf-8"), encoding="utf-8")
        print(f"\n→ report {report_path}")
        print(f"→ {report['passed']} passed, {report['failed']} failed")
        if report["failed"] > 0 and args.remediate:
            subprocess.call([str(REPO / "scripts" / "functional_remediate.sh"), str(report_path)])
        return 0 if report["failed"] == 0 else 1
    finally:
        ser.close()


if __name__ == "__main__":
    sys.exit(main())
