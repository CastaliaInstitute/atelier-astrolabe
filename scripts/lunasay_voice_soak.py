#!/usr/bin/env python3
"""Repeatable physical-device LunaSay STT -> LLM -> TTS validation.

macOS ``say`` plays known user prompts. Speech is scheduled from the HTTP
acceptance time, not from serial output: USB logging can lag behind capture and
would otherwise truncate the end of the prompt. The full serial transcript and
a machine-readable verdict are written under ``artifacts/qa``.
"""

from __future__ import annotations

import argparse
from datetime import datetime
import glob
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import time
from urllib import request

import serial


ROOT = Path(__file__).resolve().parents[1]
CRASH_RE = re.compile(r"Guru Meditation|assert failed|CORRUPT HEAP|panic'ed", re.I)
CASES = (
    ("alpha", 5000, "Reply with exactly one word: alpha.", "alpha", True),
    ("bravo", 5000, "Reply with exactly one word: bravo.", "bravo", True),
    (
        "moon",
        7000,
        "In one sentence under twelve words, explain why the moon changes shape.",
        "moon",
        False,
    ),
)

FACE_TOUR_CASES = (
    ("moon", 7000, "In one sentence under twelve words, explain why the moon changes shape.", ("moon", "sunlit", "orbit", "phase")),
    ("astrology", 7000, "In one sentence under twelve words, what does a natal chart describe?", ("birth", "natal", "planet", "celestial", "personality")),
    ("transits", 7000, "In one sentence under twelve words, what is an astrological transit?", ("planet", "movement", "current", "natal", "sky", "influence")),
    ("synastry", 7000, "In one sentence under twelve words, what does synastry compare?", ("relationship", "compatibility", "charts", "people", "two")),
    ("tarot", 7000, "In one sentence under twelve words, what can a tarot reading help someone explore?", ("choice", "pattern", "question", "possibility", "meaning", "insight", "thought", "path", "reflection")),
    ("alethiometer", 7000, "In one sentence under twelve words, what truth should I examine today?", ()),
    ("sky", 7000, "In one sentence under twelve words, name one thing visible in tonight's sky.", ("star", "planet", "moon", "constellation", "visible")),
)


def resolve_port(value: str) -> str:
    if value:
        return value
    for name in ("ASTROLABE175C_VOICE_PORT", "ESPPORT", "IDF_PORT"):
        if os.environ.get(name):
            return os.environ[name]
    ports = sorted(glob.glob("/dev/cu.usbmodem*") + glob.glob("/dev/ttyACM*"))
    if len(ports) != 1:
        raise SystemExit(f"error: expected one serial port, found {ports}")
    return ports[0]


def mac_volume() -> int:
    output = subprocess.check_output(
        ["osascript", "-e", "output volume of (get volume settings)"], text=True
    )
    return int(output.strip())


def set_mac_volume(value: int) -> None:
    subprocess.run(
        ["osascript", "-e", f"set volume output volume {value}"], check=True
    )


def http_get(url: str, timeout: float = 4.0) -> str:
    with request.urlopen(url, timeout=timeout) as response:
        return response.read().decode("utf-8", "replace")


def wait_for_full_server(ip: str, timeout: float = 45.0) -> None:
    deadline = time.monotonic() + timeout
    last_error: Exception | None = None
    while time.monotonic() < deadline:
        try:
            # Touching the thin listener requests the full HTTP/API server.
            http_get(f"http://{ip}/", timeout=2.0)
        except Exception as exc:
            last_error = exc
        try:
            http_get(f"http://{ip}/api/faces", timeout=3.0)
            return
        except Exception as exc:
            last_error = exc
            time.sleep(1.0)
    raise RuntimeError(f"full device API did not become ready: {last_error}")


def start_capture(ip: str, capture_ms: int) -> dict:
    body = json.dumps({"action": "stt", "ms": capture_ms}).encode()
    req = request.Request(
        f"http://{ip}/api/voice",
        data=body,
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    with request.urlopen(req, timeout=5.0) as response:
        return json.loads(response.read())


def select_face(ip: str, slug: str) -> dict:
    body = json.dumps({"slug": slug}).encode()
    req = request.Request(
        f"http://{ip}/api/face",
        data=body,
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    deadline = time.monotonic() + 15.0
    last_error: Exception | None = None
    while time.monotonic() < deadline:
        try:
            with request.urlopen(req, timeout=5.0) as response:
                raw = response.read()
                if raw:
                    return json.loads(raw)
        except Exception as exc:
            last_error = exc
        time.sleep(1.0)
    raise RuntimeError(f"face selection failed for {slug}: {last_error}")


def read_turn(ser: serial.Serial, timeout: float) -> list[str]:
    rows: list[str] = []
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        raw = ser.readline()
        if not raw:
            continue
        row = raw.decode("utf-8", "replace").strip()
        rows.append(row)
        if "qa: stt done err=" in row:
            break
    return rows


def match(text: str, pattern: str) -> tuple[str, ...] | None:
    found = re.search(pattern, text)
    return found.groups() if found else None


def summarize(
    name: str,
    expected: str,
    reply_must_match: bool,
    accepted: bool,
    rows: list[str],
    elapsed: float,
    semantic_terms: tuple[str, ...] | None = None,
    selected: bool = True,
) -> dict:
    text = "\n".join(rows)
    capture = match(text, r"qa: stt capture frames=(\d+) peak=(\d+) rms=(\d+) err=(\S+)")
    http = match(text, r"\[pipeline\] HTTP (\d+) mp3=(\d+)B in (\d+)ms err=(\S+)")
    play = match(text, r"\[tts\] play file done in (\d+)ms err=(\S+)")
    transcript = (match(text, r'qa: stt transcript="([^"]*)"') or ("",))[0]
    reply = (match(text, r'qa: stt reply="(.*)"') or ("",))[0]
    done = (match(text, r"qa: stt done err=(\S+)") or ("",))[0]
    if semantic_terms is not None:
        semantic_ok = bool(transcript.strip() and reply.strip()) and (
            not semantic_terms
            or any(term in reply.lower() for term in semantic_terms)
        )
        semantic_ok = semantic_ok and not re.search(
            r"\b(did not study|no observable evidence|cannot answer|won't answer)\b",
            reply,
            re.I,
        )
        if name == "alethiometer":
            try:
                reading = json.loads(reply)
                question_symbols = reading.get("questionSymbols", [])
                semantic_ok = bool(
                    len(question_symbols) == 3
                    and len(set(question_symbols)) == 3
                    and all(isinstance(value, int) and 0 <= value <= 35 for value in question_symbols)
                    and isinstance(reading.get("answerSymbol"), int)
                    and 0 <= reading["answerSymbol"] <= 35
                    and reading.get("spoken", "").strip()
                )
            except (TypeError, ValueError, json.JSONDecodeError):
                semantic_ok = False
    else:
        semantic_ok = (
            reply.strip().rstrip(".!?").lower() == expected.lower()
            if reply_must_match
            else expected.lower() in f"{transcript} {reply}".lower()
        )
    passed = bool(
        selected
        and accepted
        and capture
        and capture[3] == "ESP_OK"
        and http
        and http[0] == "200"
        and http[3] == "ESP_OK"
        and play
        and play[1] == "ESP_OK"
        and done == "ESP_OK"
        and semantic_ok
        and not CRASH_RE.search(text)
    )
    return {
        "case": name,
        "passed": passed,
        "accepted": accepted,
        "selected": selected,
        "capture": capture,
        "http": http,
        "playback": play,
        "transcript": transcript,
        "reply": reply,
        "done": done,
        "crash": bool(CRASH_RE.search(text)),
        "elapsed_s": round(elapsed, 2),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="")
    parser.add_argument("--ip", default="192.168.86.72")
    parser.add_argument("--say-rate", type=int, default=155)
    parser.add_argument("--say-delay", type=float, default=0.35)
    parser.add_argument("--volume", type=int, default=85)
    parser.add_argument("--turn-timeout", type=float, default=100.0)
    parser.add_argument("--out-dir", default="")
    parser.add_argument(
        "--face-tour",
        action="store_true",
        help="select and acoustically validate every LunaSay Kickstarter face",
    )
    parser.add_argument(
        "--faces",
        default="",
        help="comma-separated subset for --face-tour (for targeted retests)",
    )
    args = parser.parse_args()

    if shutil.which("say") is None or shutil.which("osascript") is None:
        raise SystemExit("error: this physical acoustic test requires macOS say and osascript")
    if not 0.1 <= args.say_delay <= 2.0:
        raise SystemExit("error: --say-delay must be between 0.1 and 2.0 seconds")

    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    out_dir = (
        Path(args.out_dir)
        if args.out_dir
        else ROOT / "artifacts" / "qa" / f"lunasay-voice-soak-{stamp}"
    )
    out_dir.mkdir(parents=True, exist_ok=True)
    port = resolve_port(args.port)
    original_volume = mac_volume()
    all_rows: list[str] = []
    results: list[dict] = []

    try:
        set_mac_volume(args.volume)
        with serial.Serial(port, 115200, timeout=0.2) as ser:
            # A request to the thin listener pauses the duplex audio pipeline
            # and brings up the full settings/API server.
            wait_for_full_server(args.ip)

            cases = FACE_TOUR_CASES if args.face_tour else CASES
            if args.faces:
                if not args.face_tour:
                    raise SystemExit("error: --faces requires --face-tour")
                requested = {slug.strip() for slug in args.faces.split(",") if slug.strip()}
                cases = tuple(case for case in cases if case[0] in requested)
                found = {case[0] for case in cases}
                if found != requested:
                    raise SystemExit(f"error: unknown face(s): {sorted(requested - found)}")
            for case in cases:
                if args.face_tour:
                    name, capture_ms, phrase, semantic_terms = case
                    face_response = select_face(args.ip, name)
                    selected = bool(
                        face_response.get("ok")
                        and face_response.get("face", {}).get("slug") == name
                    )
                    expected = ""
                    reply_must_match = False
                    time.sleep(1.0)
                else:
                    name, capture_ms, phrase, expected, reply_must_match = case
                    semantic_terms = None
                    selected = True
                ser.reset_input_buffer()
                started = time.monotonic()
                response = start_capture(args.ip, capture_ms)
                accepted = bool(response.get("accepted"))
                # Do not wait for the serial "capture begin" log. Its USB
                # delivery can lag seconds behind the actual capture clock.
                rows = []
                if accepted:
                    time.sleep(args.say_delay)
                    subprocess.run(
                        ["say", "-r", str(args.say_rate), phrase], check=True
                    )
                    rows = read_turn(ser, args.turn_timeout)
                all_rows.extend([f"TURN {name}", *rows])
                result = summarize(
                    name,
                    expected,
                    reply_must_match,
                    accepted,
                    rows,
                    time.monotonic() - started,
                    semantic_terms,
                    selected,
                )
                results.append(result)
                print(json.dumps(result), flush=True)
    finally:
        set_mac_volume(original_volume)

    verdict = {
        "passed": len(results) == len(cases) and all(row["passed"] for row in results),
        "port": port,
        "ip": args.ip,
        "say_delay_s": args.say_delay,
        "results": results,
    }
    (out_dir / "serial.log").write_text("\n".join(all_rows) + "\n", encoding="utf-8")
    (out_dir / "summary.json").write_text(
        json.dumps(verdict, indent=2) + "\n", encoding="utf-8"
    )
    print(f"lunasay_voice_soak: {'PASS' if verdict['passed'] else 'FAIL'} {out_dir}")
    return 0 if verdict["passed"] else 2


if __name__ == "__main__":
    raise SystemExit(main())
