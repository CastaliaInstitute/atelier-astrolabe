#!/usr/bin/env python3
"""Run an all-face Astrolabe STT->reply->TTS hardware tour.

The harness walks every ported face reported by `faces list`, triggers the
fixed-window QA STT path on that face, speaks a known host prompt into the
watch, and waits for `qa: stt done err=ESP_OK`. That marker is emitted only
after the reply MP3 has been played back, so each passing row covers STT,
backend reply, and TTS playback for the current face.
"""

from __future__ import annotations

import argparse
import glob
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time
from datetime import datetime

try:
    import serial
except ModuleNotFoundError:
    ROOT_FOR_VENV = Path(__file__).resolve().parents[1]
    venv_python = ROOT_FOR_VENV / "mcp" / "astrolabe-esp" / ".venv" / "bin" / "python"
    if venv_python.exists() and Path(sys.executable) != venv_python:
        os.execv(str(venv_python), [str(venv_python), *sys.argv])
    raise


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_PHRASE = "What is one precise way to test a small machine?"
CRASH_RE = re.compile(r"Guru Meditation|Backtrace:|Stack canary|stack overflow|panic|Brownout|ASTROLABE_ALERT", re.I)
REBOOT_RE = re.compile(r"\brst:|ESP-ROM:|boot: ESP-IDF", re.I)
FACE_RE = re.compile(
    r"^face:\s+(?P<slug>\S+)\s+enabled=(?P<enabled>\w+)\s+nav=(?P<nav>\w+)\s+"
    r"order=(?P<order>\d+)\s+ported=(?P<ported>\w+)\s+categories=(?P<categories>0x[0-9a-fA-F]+)"
    r"(?P<current>\s+current)?$"
)
DONE_RE = re.compile(r"qa: stt done err=(?P<err>\S+)")
TRANSCRIPT_RE = re.compile(r"qa: stt transcript=\"(?P<text>.*)\"")
REPLY_RE = re.compile(r"qa: stt reply=\"(?P<text>.*)\"")
SET_RE = re.compile(r"faces: set (?P<slug>\S+) (?P<err>\S+)")
TRIGGER_RE = re.compile(r"(?:stt: capture_ms=\d+|qa: stt trigger ms=\d+) (?P<err>\S+)")
CAPTURE_RE = re.compile(r"qa: stt capture frames=(?P<frames>\d+) peak=(?P<peak>-?\d+) rms=(?P<rms>\d+) err=(?P<err>\S+)")


def resolve_port(port: str | None) -> str:
    if port:
        return port
    for name in (
        "ASTROLABE175C_VOICE_PORT",
        "ASTROLABE_SERIAL_PORT",
        "ASTROLABE_UPLOAD_PORT",
        "UPLOAD_PORT",
        "ESPPORT",
        "IDF_PORT",
    ):
        value = os.environ.get(name)
        if value:
            return value
    ports = sorted(
        glob.glob("/dev/cu.usbmodem*")
        + glob.glob("/dev/tty.usbmodem*")
        + glob.glob("/dev/ttyACM*")
        + glob.glob("/dev/ttyUSB*")
    )
    if len(ports) == 1:
        return ports[0]
    if not ports:
        raise SystemExit("error: no Astrolabe serial port found")
    raise SystemExit(f"error: multiple serial ports found: {', '.join(ports)}")


def host_speech_command(phrase: str, rendered_path: Path | None = None) -> list[str]:
    say = shutil.which("say")
    if say is not None:
        afplay = shutil.which("afplay")
        if rendered_path is not None and afplay is not None:
            render = subprocess.run(
                [say, "-o", str(rendered_path), phrase],
                text=True,
                check=False,
                capture_output=True,
            )
            if render.returncode == 0 and rendered_path.exists() and rendered_path.stat().st_size > 0:
                # Software gain keeps the laptop-speaker fixture comfortably
                # above the cloud STT floor without changing system volume.
                return [afplay, "-v", "2", str(rendered_path)]
        return [say, phrase]
    spd_say = shutil.which("spd-say")
    if spd_say is not None:
        return [spd_say, "-w", phrase]
    espeak_ng = shutil.which("espeak-ng")
    if espeak_ng is not None:
        return [espeak_ng, phrase]
    espeak = shutil.which("espeak")
    if espeak is not None:
        return [espeak, phrase]
    raise SystemExit("error: no host speech command found (need say, spd-say, espeak-ng, or espeak)")


def write_cmd(ser: serial.Serial, cmd: str) -> None:
    print(f"> {cmd}", flush=True)
    ser.write((cmd + "\n").encode("utf-8"))
    ser.flush()


def read_lines_until(ser: serial.Serial, timeout_s: float, stop: tuple[str, ...] = ()) -> list[tuple[float, str]]:
    deadline = time.monotonic() + timeout_s
    buf = ""
    lines: list[tuple[float, str]] = []
    while time.monotonic() < deadline:
        chunk = ser.read(4096)
        if not chunk:
            time.sleep(0.03)
            continue
        buf += chunk.decode("utf-8", errors="replace")
        while "\n" in buf:
            line, buf = buf.split("\n", 1)
            line = line.rstrip("\r")
            ts = time.time()
            lines.append((ts, line))
            print(line, flush=True)
            if any(needle in line for needle in stop):
                return lines
    if buf:
        line = buf.rstrip("\r")
        lines.append((time.time(), line))
        print(line, flush=True)
    return lines


def command_lines(ser: serial.Serial, cmd: str, timeout_s: float, stop: tuple[str, ...]) -> list[tuple[float, str]]:
    write_cmd(ser, cmd)
    return read_lines_until(ser, timeout_s, stop)


def parse_faces(text: str, include_unported: bool) -> list[dict]:
    faces = []
    for line in text.splitlines():
        m = FACE_RE.match(line.strip())
        if not m:
            continue
        face = m.groupdict()
        face["order"] = int(face["order"])
        face["enabled"] = face["enabled"] == "yes"
        face["nav"] = face["nav"] == "yes"
        face["ported"] = face["ported"] == "yes"
        face["current"] = bool(face["current"])
        if include_unported or face["ported"]:
            faces.append(face)
    return faces


def summarize_turn(slug: str,
                   set_rows: list[tuple[float, str]],
                   begin_rows: list[tuple[float, str]],
                   turn_rows: list[tuple[float, str]],
                   elapsed_ms: float,
                   speech_returncode: int | None) -> dict:
    rows = set_rows + begin_rows + turn_rows
    text = "\n".join(line for _, line in rows)
    set_match = SET_RE.search("\n".join(line for _, line in set_rows))
    trigger = TRIGGER_RE.search("\n".join(line for _, line in begin_rows))
    capture = CAPTURE_RE.search(text)
    done = DONE_RE.search(text)
    transcript = TRANSCRIPT_RE.search(text)
    reply = REPLY_RE.search(text)
    crashes = [line for _, line in rows if CRASH_RE.search(line)]
    reboots = [line for _, line in rows if REBOOT_RE.search(line)]
    set_err = set_match.group("err") if set_match else "MISSING"
    trigger_err = trigger.group("err") if trigger else "MISSING"
    err = done.group("err") if done else "TIMEOUT"
    failures = []
    if set_err != "ESP_OK":
        failures.append(f"faces set {set_err}")
    if trigger_err != "ESP_OK":
        failures.append(f"stt trigger {trigger_err}")
    if speech_returncode not in (0, None):
        failures.append(f"host speech exited {speech_returncode}")
    if err != "ESP_OK":
        failures.append(f"stt done {err}")
    if transcript is None:
        failures.append("missing transcript")
    if reply is None:
        failures.append("missing reply")
    if crashes:
        failures.append("crash marker")
    if reboots:
        failures.append("reboot marker")
    ok = (
        set_err == "ESP_OK"
        and trigger_err == "ESP_OK"
        and err == "ESP_OK"
        and transcript is not None
        and reply is not None
        and speech_returncode in (0, None)
        and not crashes
        and not reboots
    )
    return {
        "slug": slug,
        "ok": ok,
        "failure": "; ".join(failures),
        "set_err": set_err,
        "trigger_err": trigger_err,
        "err": err,
        "elapsed_ms": round(elapsed_ms, 1),
        "speech_returncode": speech_returncode,
        "capture_frames": int(capture.group("frames")) if capture else None,
        "capture_peak": int(capture.group("peak")) if capture else None,
        "capture_rms": int(capture.group("rms")) if capture else None,
        "capture_err": capture.group("err") if capture else "",
        "transcript": transcript.group("text") if transcript else "",
        "reply": reply.group("text") if reply else "",
        "crashes": crashes,
        "reboots": reboots,
        "line_count": len(rows),
    }


def run_self_test() -> int:
    face_text = "\n".join([
        "faces: current=faculty profile=default count=3",
        "face: faculty      enabled=yes nav=yes order=10 ported=yes categories=0x01 current",
        "face: pythia       enabled=no nav=no order=165 ported=no categories=0x04",
        "face: quotes       enabled=yes nav=yes order=20 ported=yes categories=0x02",
    ])
    faces = parse_faces(face_text, include_unported=False)
    assert [face["slug"] for face in faces] == ["faculty", "quotes"], faces

    ok = summarize_turn(
        "faculty",
        [(1.0, "faces: set faculty ESP_OK")],
        [(2.0, "stt: capture_ms=6500 ESP_OK"), (3.0, "qa: stt capture begin ms=6500")],
        [
            (4.0, "qa: stt capture frames=120 peak=1234 rms=456 err=ESP_OK"),
            (5.0, "qa: stt transcript=\"hello\""),
            (6.0, "qa: stt reply=\"world\""),
            (7.0, "qa: stt done err=ESP_OK"),
        ],
        1000.0,
        0,
    )
    assert ok["ok"] is True, ok
    assert ok["capture_frames"] == 120, ok

    fail = summarize_turn(
        "quotes",
        [(1.0, "faces: set quotes ESP_OK")],
        [(2.0, "stt: capture_ms=6500 ESP_OK")],
        [(3.0, "Guru Meditation Error: Core 0 panic'ed"), (4.0, "qa: stt done err=ESP_FAIL")],
        2000.0,
        0,
    )
    assert fail["ok"] is False, fail
    assert "crash marker" in fail["failure"], fail
    assert fail["err"] == "ESP_FAIL", fail
    verify_ok = verify_summary_data({
        "limited": False,
        "planned_face_count": 2,
        "completed_face_count": 2,
        "ok_count": 2,
        "fail_count": 0,
        "crash_count": 0,
        "reboot_count": 0,
        "missing_faces": [],
        "results": [ok | {"slug": "faculty"}, ok | {"slug": "quotes"}],
    })
    assert verify_ok == [], verify_ok
    verify_fail = verify_summary_data({
        "limited": True,
        "planned_face_count": 2,
        "completed_face_count": 1,
        "ok_count": 1,
        "fail_count": 0,
        "crash_count": 0,
        "reboot_count": 0,
        "missing_faces": ["quotes"],
        "results": [ok | {"slug": "faculty"}],
    })
    assert any("limited" in item for item in verify_fail), verify_fail
    print("stt_tts_face_tour: self-test passed")
    return 0


def verify_summary_data(summary: dict, allow_limited: bool = False) -> list[str]:
    failures = []
    planned = int(summary.get("planned_face_count", summary.get("face_count", 0)) or 0)
    completed = int(summary.get("completed_face_count", summary.get("face_count", 0)) or 0)
    ok_count = int(summary.get("ok_count", 0) or 0)
    fail_count = int(summary.get("fail_count", 0) or 0)
    crash_count = int(summary.get("crash_count", 0) or 0)
    reboot_count = int(summary.get("reboot_count", 0) or 0)
    limited = bool(summary.get("limited", False))
    missing = summary.get("missing_faces") or []
    results = summary.get("results") or []

    if limited and not allow_limited:
        failures.append("summary is from a limited shakedown run")
    if planned <= 0:
        failures.append("planned_face_count is zero")
    if completed != planned:
        failures.append(f"completed {completed} of {planned} planned faces")
    if missing:
        failures.append(f"missing faces: {', '.join(str(item) for item in missing)}")
    if ok_count != planned:
        failures.append(f"ok_count {ok_count} does not match planned_face_count {planned}")
    if fail_count != 0:
        failures.append(f"fail_count is {fail_count}")
    if crash_count != 0:
        failures.append(f"crash_count is {crash_count}")
    if reboot_count != 0:
        failures.append(f"reboot_count is {reboot_count}")
    bad_results = [str(row.get("slug", row.get("index", "?"))) for row in results if not row.get("ok")]
    if bad_results:
        failures.append(f"failed result rows: {', '.join(bad_results)}")
    return failures


def verify_summary_file(path: Path, allow_limited: bool = False) -> int:
    summary = json.loads(path.read_text(encoding="utf-8"))
    failures = verify_summary_data(summary, allow_limited=allow_limited)
    if failures:
        print(f"stt_tts_face_tour: verify failed {path}", file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}", file=sys.stderr)
        return 2
    print(
        "stt_tts_face_tour: verify ok "
        f"{path} faces={summary.get('planned_face_count', summary.get('face_count'))}",
        flush=True,
    )
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="")
    parser.add_argument("--phrase", default=DEFAULT_PHRASE)
    parser.add_argument("--capture-ms", type=int, default=6500)
    parser.add_argument("--turn-timeout-sec", type=float, default=75.0)
    parser.add_argument("--settle-sec", type=float, default=1.0)
    parser.add_argument("--out-dir", default="")
    parser.add_argument("--include-unported", action="store_true")
    parser.add_argument(
        "--nav-only",
        action="store_true",
        help="Test only faces in the active profile's swipe navigation roster",
    )
    parser.add_argument("--limit", type=int, default=0, help="Limit face count for shakedown runs")
    parser.add_argument(
        "--live-say",
        action="store_true",
        help="Invoke macOS say at capture time instead of replaying a pre-rendered prompt",
    )
    parser.add_argument("--self-test", action="store_true", help="Run parser/verdict checks without hardware")
    parser.add_argument("--verify-summary", default="", help="Verify an existing summary.json and exit")
    parser.add_argument("--allow-limited", action="store_true", help="Allow --verify-summary to accept limited runs")
    args = parser.parse_args()

    if args.self_test:
        return run_self_test()
    if args.verify_summary:
        return verify_summary_file(Path(args.verify_summary), allow_limited=args.allow_limited)

    if args.capture_ms < 1000 or args.capture_ms > 15000:
        raise SystemExit("error: --capture-ms must be between 1000 and 15000")

    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    out_dir = Path(args.out_dir) if args.out_dir else ROOT / "artifacts" / "qa" / f"stt-tts-face-tour-{stamp}"
    out_dir.mkdir(parents=True, exist_ok=True)
    speech_cmd = host_speech_command(
        args.phrase,
        None if args.live_say else out_dir / "host-prompt.aiff",
    )
    serial_log_path = out_dir / "serial-timestamped.log"
    summary_path = out_dir / "summary.json"

    port = resolve_port(args.port)
    print(f"stt_tts_face_tour: port={port} out={out_dir}", flush=True)
    print("+", " ".join(speech_cmd), flush=True)

    all_rows: list[tuple[float, str]] = []
    results: list[dict] = []
    with serial.Serial(port, 115200, timeout=0.25) as ser:
        time.sleep(0.8)
        ser.reset_input_buffer()
        list_rows = command_lines(ser, "faces list", 4.0, ())
        all_rows.extend(list_rows)
        faces = parse_faces("\n".join(line for _, line in list_rows), args.include_unported)
        if args.nav_only:
            faces = [face for face in faces if face["nav"]]
        planned_faces = [face["slug"] for face in faces]
        if args.limit > 0:
            faces = faces[: args.limit]
        if not faces:
            raise SystemExit("error: no faces parsed from `faces list`")

        for index, face in enumerate(faces, start=1):
            slug = face["slug"]
            print(f"stt_tts_face_tour: face {index}/{len(faces)} {slug}", flush=True)
            set_rows = command_lines(ser, f"faces set {slug}", 8.0, (f"faces: set {slug}",))
            all_rows.extend(set_rows)
            time.sleep(args.settle_sec)
            start = time.perf_counter()
            write_cmd(ser, f"voice stt {args.capture_ms}")
            begin_rows = read_lines_until(ser, 8.0, ("qa: stt capture begin", "stt: capture_ms="))
            trigger = TRIGGER_RE.search("\n".join(line for _, line in begin_rows))
            trigger_failed = trigger is not None and trigger.group("err") != "ESP_OK"
            capture_started = any("qa: stt capture begin" in line for _, line in begin_rows)
            if not trigger_failed and not capture_started:
                ready_rows = read_lines_until(ser, 20.0, ("qa: stt capture begin", "qa: stt done err="))
                begin_rows.extend(ready_rows)
                capture_started = any("qa: stt capture begin" in line for _, line in ready_rows)
            all_rows.extend(begin_rows)
            speech_returncode = None
            if capture_started:
                print(f"+ face={slug}", " ".join(speech_cmd), flush=True)
                speech_returncode = subprocess.run(speech_cmd, text=True, check=False).returncode
                turn_rows = read_lines_until(ser, args.turn_timeout_sec, ("qa: stt done err=",))
            else:
                already_done = any("qa: stt done err=" in line for _, line in begin_rows)
                turn_rows = [] if already_done else read_lines_until(ser, 2.0, ("qa: stt done err=",))
            all_rows.extend(turn_rows)
            result = summarize_turn(slug, set_rows, begin_rows, turn_rows,
                                    (time.perf_counter() - start) * 1000,
                                    speech_returncode)
            result.update({
                "index": index,
                "ported": face["ported"],
                "enabled": face["enabled"],
                "nav": face["nav"],
                "categories": face["categories"],
            })
            results.append(result)
            if result["crashes"] or result["reboots"]:
                print(f"stt_tts_face_tour: aborting after crash/reboot on {slug}", flush=True)
                break

    with serial_log_path.open("w", encoding="utf-8") as log:
        for ts, line in all_rows:
            log.write(f"{ts:.3f} {line}\n")

    summary = {
        "timestamp": stamp,
        "port": port,
        "phrase": args.phrase,
        "capture_ms": args.capture_ms,
        "nav_only": args.nav_only,
        "limited": args.limit > 0,
        "planned_face_count": len(faces),
        "available_face_count": len(planned_faces),
        "completed_face_count": len(results),
        "face_count": len(results),
        "planned_faces": [face["slug"] for face in faces],
        "available_faces": planned_faces,
        "missing_faces": [face["slug"] for face in faces[len(results):]],
        "ok_count": sum(1 for result in results if result["ok"]),
        "fail_count": sum(1 for result in results if not result["ok"]),
        "crash_count": sum(len(result["crashes"]) for result in results),
        "reboot_count": sum(len(result["reboots"]) for result in results),
        "results": results,
        "serial_log": str(serial_log_path),
    }
    summary_path.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(f"stt_tts_face_tour: summary={summary_path}", flush=True)
    print(
        "stt_tts_face_tour: "
        f"ok={summary['ok_count']} fail={summary['fail_count']} "
        f"crash={summary['crash_count']} reboot={summary['reboot_count']}",
        flush=True,
    )
    return 0 if summary["fail_count"] == 0 and summary["face_count"] > 0 else 2


if __name__ == "__main__":
    raise SystemExit(main())
