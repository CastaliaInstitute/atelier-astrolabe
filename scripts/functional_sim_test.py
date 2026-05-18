#!/usr/bin/env python3
"""Serial-only functional tests in QEMU (no Wi‑Fi / screen.bmp)."""
from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
import time
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
DEFAULT_MATRIX = REPO / "tests" / "functional" / "faces_qemu.json"
OUT_DIR = REPO / "artifacts" / "functional-sim"

CRASH_PATTERNS = [
    re.compile(r"Guru Meditation", re.I),
    re.compile(r"abort\(\)", re.I),
    re.compile(r"Backtrace:", re.I),
    re.compile(r"Stack overflow", re.I),
    re.compile(r"panic'ed", re.I),
    re.compile(r"Brownout detector", re.I),
]

READY_PATTERNS = [
    re.compile(r"PocketMynah MVP ready", re.I),
    re.compile(r"Astrolabe ready", re.I),
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
    serial_log: str = ""


class QemuSerial:
    def __init__(self, proc: subprocess.Popen[str]) -> None:
        self._proc = proc
        self._buf = ""

    def close(self) -> None:
        if self._proc.stdin:
            self._proc.stdin.close()
        self._proc.terminate()
        try:
            self._proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self._proc.kill()

    def _drain(self, timeout: float = 0.0) -> list[str]:
        lines: list[str] = []
        deadline = time.time() + timeout
        while time.time() < deadline:
            if self._proc.stdout is None:
                break
            chunk = self._proc.stdout.read(4096)
            if chunk:
                self._buf += chunk
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
        if not self._proc.stdin:
            return []
        self._proc.stdin.write(cmd.strip() + "\n")
        self._proc.stdin.flush()
        return self._drain(wait)

    def wait_ready(self, timeout: float = 120.0) -> bool:
        deadline = time.time() + timeout
        while time.time() < deadline:
            for ln in self._drain(0.2):
                for pat in READY_PATTERNS:
                    if pat.search(ln):
                        return True
        return False

    def wait_line(self, pattern: re.Pattern[str], timeout: float = 12.0) -> str | None:
        deadline = time.time() + timeout
        while time.time() < deadline:
            for ln in self._drain(0.15):
                if pattern.search(ln):
                    return ln
        return None

    @staticmethod
    def check_crashes(lines: list[str]) -> str | None:
        for ln in lines:
            for pat in CRASH_PATTERNS:
                if pat.search(ln):
                    return ln
        return None


def gesture_to_qa(g: str) -> str:
    if g.startswith("tap:"):
        xy = g.split(":", 1)[1]
        x, y = xy.split(",")
        return f"qa inject tap {x} {y}"
    if g.startswith("swipe_"):
        return f"qa inject swipe {g.replace('swipe_', '')}"
    raise ValueError(g)


def button_to_qa(b: str) -> str:
    if b == "boot":
        return "qa inject boot"
    if b == "pwr":
        return "qa inject pwr"
    raise ValueError(b)


def run_face(ser: QemuSerial, spec: dict, out_dir: Path) -> FaceResult:
    fid = int(spec["id"])
    name = spec["name"]
    result = FaceResult(face_id=fid, name=name, ok=True)
    log: list[str] = []

    def step(n: str, ok: bool, detail: str = "") -> None:
        result.steps.append(StepResult(n, ok, detail))
        if not ok:
            result.ok = False

    lines = ser.send(f"face {fid}", wait=0.8)
    log.extend(lines)
    if not ser.wait_line(re.compile(rf"face:\s*{fid}\b"), timeout=10.0):
        step("set_face", False, "no face: ack")
        return result
    step("set_face", True)

    err = ser.check_crashes(lines)
    if err:
        step("after_set_face", False, err)
        return result

    for g in spec.get("gestures", []):
        glines = ser.send(gesture_to_qa(g), wait=0.5)
        log.extend(glines)
        err = ser.check_crashes(glines)
        step(f"gesture:{g}", err is None, err or "ok")

    for b in spec.get("buttons", []):
        blines = ser.send(button_to_qa(b), wait=0.5)
        log.extend(blines)
        err = ser.check_crashes(blines)
        step(f"button:{b}", err is None, err or "ok")

    status = ser.send("qa status", wait=0.4)
    log.extend(status)
    err = ser.check_crashes(status)
    has_qa = any(ln.startswith("qa: face=") for ln in status)
    step("qa_status", err is None and has_qa, err or ("ok" if has_qa else "no qa line"))

    log_path = out_dir / f"{fid:02d}-{name}-serial.log"
    log_path.write_text("\n".join(log) + "\n", encoding="utf-8")
    result.serial_log = str(log_path)
    return result


def launch_qemu(flash_bin: Path, qemu_bin: str, timeout_sec: int) -> QemuSerial:
    cmd = [
        qemu_bin,
        "-nographic",
        "-machine",
        "esp32s3",
        "-drive",
        f"file={flash_bin},if=mtd,format=raw",
        "-serial",
        "mon:stdio",
        "-monitor",
        "none",
    ]
    proc = subprocess.Popen(
        cmd,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        bufsize=0,
    )
    return QemuSerial(proc)


def main() -> int:
    parser = argparse.ArgumentParser(description="Astrolabe QEMU sim tests (serial only)")
    parser.add_argument("--matrix", type=Path, default=DEFAULT_MATRIX)
    parser.add_argument("--flash-bin", type=Path, required=True)
    parser.add_argument("--qemu", default="", help="qemu-system-xtensa path")
    parser.add_argument("--ready-timeout", type=float, default=120.0)
    parser.add_argument("--output", type=Path, default=OUT_DIR)
    args = parser.parse_args()

    qemu = args.qemu.strip() or __import__("os").environ.get("QEMU_ESP32", "qemu-system-xtensa")
    matrix = json.loads(args.matrix.read_text(encoding="utf-8"))
    stamp = datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%S")
    out_dir = args.output / stamp
    out_dir.mkdir(parents=True, exist_ok=True)

    ser = launch_qemu(args.flash_bin, qemu, int(args.ready_timeout))
    try:
        if not ser.wait_ready(timeout=args.ready_timeout):
            print("error: QEMU did not reach ready banner", file=sys.stderr)
            return 1
        print("→ QEMU ready")

        results = []
        for spec in matrix["faces"]:
            print(f"→ face {spec['id']} {spec['name']}")
            fr = run_face(ser, spec, out_dir)
            results.append(fr)
            print("  PASS" if fr.ok else "  FAIL")

        try:
            git_head = subprocess.check_output(
                ["git", "rev-parse", "HEAD"], cwd=str(REPO), text=True
            ).strip()
        except subprocess.CalledProcessError:
            git_head = ""

        report = {
            "timestamp": stamp,
            "gate": "sim",
            "matrix": str(args.matrix),
            "git_head": git_head,
            "passed": sum(1 for r in results if r.ok),
            "failed": sum(1 for r in results if not r.ok),
            "faces": [
                {
                    "id": r.face_id,
                    "name": r.name,
                    "ok": r.ok,
                    "serial_log": r.serial_log,
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
        print(f"→ report {report_path}")
        print(f"→ {report['passed']} passed, {report['failed']} failed")
        return 0 if report["failed"] == 0 else 1
    finally:
        ser.close()


if __name__ == "__main__":
    sys.exit(main())
