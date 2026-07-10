#!/usr/bin/env python3
"""Repeated Faculty voice soak on the real 1.75C path.

This drives the attached watch over USB serial, triggers the streaming capture
pipeline, plays a repeatable rendered prompt through the host speakers, and
collects memory status before/after each turn so we can detect heap drift over
repeated use.
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

import serial
import serial.tools.list_ports


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_PHRASE = "What are you doing today?"
DEFAULT_DEVICE_MAC = "a4:cb:8f:d6:41:94"

STATUS_RE = re.compile(
    r"qa: face=(?P<face>\S+) lcd=(?P<lcd>\d+x\d+) audio=(?P<audio>\S+) .* "
    r"heap=(?P<heap>\d+) internal=(?P<internal>\d+) largest=(?P<largest>\d+) "
    r"psram=(?P<psram>\d+) wifi_rssi=(?P<rssi>-?\d+) ch=(?P<ch>\d+)"
)
PIPELINE_HEAP_RE = re.compile(
    r"\[heap\]\s+pipeline-(?P<stage>[a-z0-9\-]+)\s+internal=(?P<internal>\d+)\s+"
    r"largest=(?P<largest>\d+)\s+psram=(?P<psram>\d+)"
)
TASK_RE = re.compile(
    r"qa:\s+task\s+name=(?P<name>\S+)\s+stack=(?P<stack>\d+)\s+"
    r"(?:(?:state=(?P<state>\w+))|(?:free=(?P<free>\d+)\s+used_peak=(?P<used_peak>\d+)))"
)
PIPELINE_STATUS_RE = re.compile(
    r"pipeline:\s+configured=(?P<configured>\w+)\s+created=(?P<created>\w+)\s+"
    r"started=(?P<started>\w+)\s+starting=(?P<starting>\w+)"
)


def normalize_mac(value: str) -> str:
    return value.strip().lower().replace(":", "").replace("-", "")


def list_usbmodem_ports() -> list[serial.tools.list_ports_common.ListPortInfo]:
    return [info for info in serial.tools.list_ports.comports() if "usbmodem" in info.device]


def describe_usbmodem_ports() -> str:
    ports = list_usbmodem_ports()
    if not ports:
        return "none"
    parts = []
    for info in ports:
        parts.append(f"{info.device} mac={info.serial_number or '?'} desc={info.description}")
    return "; ".join(parts)


def resolve_port(port: str | None, device_mac: str | None) -> str:
    if port:
        return port
    wanted = normalize_mac(device_mac or "")
    if wanted:
        for info in list_usbmodem_ports():
            hwid = normalize_mac(info.hwid)
            serial_number = normalize_mac(info.serial_number or "")
            if wanted in hwid or wanted == serial_number:
                return info.device
        attached = describe_usbmodem_ports()
        raise SystemExit(
            "error: preferred device MAC "
            f"{device_mac} is not attached; visible usbmodem ports: {attached}"
        )
    preferred = os.environ.get("ASTROLABE175C_VOICE_PORT") or os.environ.get("ASTROLABE_UPLOAD_PORT")
    if preferred:
        return preferred
    exact = "/dev/cu.usbmodem101"
    if Path(exact).exists():
        return exact
    ports = sorted(glob.glob("/dev/cu.usbmodem*"))
    if len(ports) == 1:
        return ports[0]
    if not ports:
        raise SystemExit("error: no /dev/cu.usbmodem* serial ports found")
    raise SystemExit(f"error: multiple serial ports found: {', '.join(ports)}")


def open_serial_quiet(port: str, baudrate: int = 115200, timeout: float = 0.2) -> serial.Serial:
    ser = serial.Serial()
    ser.port = port
    ser.baudrate = baudrate
    ser.timeout = timeout
    ser.dsrdtr = False
    ser.rtscts = False
    ser.dtr = False
    ser.rts = False
    ser.open()
    ser.setDTR(False)
    ser.setRTS(False)
    return ser


def reopen_serial(ser: serial.Serial, settle_s: float = 2.5, wait_s: float = 20.0) -> serial.Serial:
    port = ser.port
    baudrate = ser.baudrate
    timeout = ser.timeout if ser.timeout is not None else 0.2
    try:
        ser.close()
    except Exception:
        pass
    end = time.time() + wait_s
    while time.time() < end:
        if Path(port).exists():
            try:
                reopened = open_serial_quiet(port, baudrate=baudrate, timeout=timeout)
                time.sleep(settle_s)
                return reopened
            except serial.SerialException:
                time.sleep(0.25)
                continue
        time.sleep(0.1)
    raise serial.SerialException(f"timed out reopening {port}")


def read_until_quiet(ser: serial.Serial, quiet_s: float, max_s: float) -> tuple[serial.Serial, str]:
    end = time.time() + max_s
    quiet_deadline = time.time() + quiet_s
    chunks: list[bytes] = []
    while time.time() < end:
        try:
            chunk = ser.read(4096)
        except serial.SerialException:
            ser = reopen_serial(ser)
            quiet_deadline = time.time() + quiet_s
            continue
        if chunk:
            chunks.append(chunk)
            quiet_deadline = time.time() + quiet_s
        elif time.time() >= quiet_deadline:
            break
    return ser, b"".join(chunks).decode("utf-8", "replace")


def read_until_any(ser: serial.Serial, needles: tuple[str, ...], timeout_s: float) -> tuple[serial.Serial, str]:
    end = time.time() + timeout_s
    chunks: list[bytes] = []
    text = ""
    while time.time() < end:
        try:
            chunk = ser.read(4096)
        except serial.SerialException:
            ser = reopen_serial(ser)
            continue
        if chunk:
            chunks.append(chunk)
            text = b"".join(chunks).decode("utf-8", "replace")
            if any(needle in text for needle in needles):
                return ser, text
    return ser, text


def turn_needs_completion_wait(turn_log: str) -> bool:
    if "[turn] done" in turn_log:
        return False
    return (
        "response.done" in turn_log
        or "[speak] " in turn_log
        or "play start" in turn_log
        or "pipeline-speaking" in turn_log
    )


def completion_wait_timeout(turn_log: str, base_timeout_s: float) -> float:
    if "[speak] " in turn_log or "play start" in turn_log or "pipeline-speaking" in turn_log:
        return max(20.0, base_timeout_s)
    return max(8.0, base_timeout_s / 2.0)


def turn_has_hard_failure(turn_log: str) -> bool:
    return (
        "ESP_ERR_NO_MEM" in turn_log
        or "TG1WDT_SYS_RST" in turn_log
        or "Guru Meditation Error" in turn_log
        or "voice fail" in turn_log
        or "No speech detected" in turn_log
    )


def turn_needs_recovery_wait(turn_log: str) -> bool:
    if "[turn] done" in turn_log or turn_has_hard_failure(turn_log):
        return False
    return (
        "voice-stream websocket error" in turn_log
        or "voice-stream websocket disconnected" in turn_log
        or "input_audio_buffer.committed" in turn_log
        or "conversation.item.input_audio_transcription.completed" in turn_log
    )


def parse_status(text: str) -> dict[str, int | str] | None:
    match = STATUS_RE.search(text)
    if not match:
        return None
    data: dict[str, int | str] = {
        "face": match.group("face"),
        "lcd": match.group("lcd"),
        "audio": match.group("audio"),
    }
    for key in ("heap", "internal", "largest", "psram", "rssi", "ch"):
        data[key] = int(match.group(key))
    return data


def parse_pipeline_heaps(text: str) -> list[dict[str, int | str]]:
    heaps: list[dict[str, int | str]] = []
    for match in PIPELINE_HEAP_RE.finditer(text):
        heaps.append(
            {
                "stage": match.group("stage"),
                "internal": int(match.group("internal")),
                "largest": int(match.group("largest")),
                "psram": int(match.group("psram")),
            }
        )
    return heaps


def parse_tasks(text: str) -> list[dict[str, int | str]]:
    tasks: list[dict[str, int | str]] = []
    for match in TASK_RE.finditer(text):
        task: dict[str, int | str] = {
            "name": match.group("name"),
            "stack": int(match.group("stack")),
        }
        if match.group("state") is not None:
            task["state"] = match.group("state")
        else:
            task["free"] = int(match.group("free"))
            task["used_peak"] = int(match.group("used_peak"))
        tasks.append(task)
    return tasks


def parse_pipeline_status(text: str) -> dict[str, bool] | None:
    match = PIPELINE_STATUS_RE.search(text)
    if not match:
        return None
    return {
        "configured": match.group("configured") == "yes",
        "created": match.group("created") == "yes",
        "started": match.group("started") == "yes",
        "starting": match.group("starting") == "yes",
    }


def wait_for_connected_status(
    ser: serial.Serial,
    internal_min: int,
    timeout_s: float,
) -> tuple[serial.Serial, str, dict[str, int | str] | None]:
    end = time.time() + timeout_s
    last_text = ""
    last_status: dict[str, int | str] | None = None
    while time.time() < end:
        ser, text = send_command(ser, "qa status")
        status = parse_status(text)
        last_text = text
        last_status = status
        if status and int(status["rssi"]) != 0 and int(status["internal"]) >= internal_min:
            return ser, text, status
        time.sleep(1.0)
    return ser, last_text, last_status


def summarize_task_peaks(results: list[dict[str, object]]) -> dict[str, dict[str, int | str]]:
    summary: dict[str, dict[str, int | str]] = {}
    for result in results:
        turn = int(result["turn"])
        for phase in ("before_tasks", "after_tasks"):
            for task in result.get(phase, []):
                if not isinstance(task, dict):
                    continue
                name = str(task.get("name", ""))
                if not name:
                    continue
                entry = summary.setdefault(name, {"stack": int(task.get("stack", 0)), "max_used_peak": 0, "min_free": 1 << 30, "last_turn": turn})
                if "used_peak" in task:
                    entry["max_used_peak"] = max(int(entry["max_used_peak"]), int(task["used_peak"]))
                if "free" in task:
                    entry["min_free"] = min(int(entry["min_free"]), int(task["free"]))
                entry["last_turn"] = turn
    for entry in summary.values():
        if entry["min_free"] == 1 << 30:
            entry["min_free"] = -1
    return summary


def evaluate_failures(results: list[dict[str, object]], min_internal: int, min_largest: int) -> list[str]:
    failures: list[str] = []
    if not results:
        return ["no turns completed"]
    for result in results:
        turn = int(result["turn"])
        if not result.get("ok"):
            failures.append(f"turn {turn} did not complete cleanly")
        if result.get("pipeline_before") is not None:
            before = result["pipeline_before"]
            if isinstance(before, dict) and not before.get("configured", False):
                failures.append(f"turn {turn} pipeline not configured before capture")
        if result.get("pipeline_after") is not None:
            after = result["pipeline_after"]
            if isinstance(after, dict) and not after.get("started", False):
                failures.append(f"turn {turn} pipeline not started after turn")
        heaps = result.get("pipeline_heaps", [])
        if isinstance(heaps, list) and heaps:
            turn_min_internal = min(int(h["internal"]) for h in heaps if isinstance(h, dict) and "internal" in h)
            turn_min_largest = min(int(h["largest"]) for h in heaps if isinstance(h, dict) and "largest" in h)
            if turn_min_internal < min_internal:
                failures.append(f"turn {turn} pipeline internal heap dipped below {min_internal} ({turn_min_internal})")
            if turn_min_largest < min_largest:
                failures.append(f"turn {turn} largest internal block dipped below {min_largest} ({turn_min_largest})")
    deduped: list[str] = []
    for failure in failures:
        if failure not in deduped:
            deduped.append(failure)
    return deduped


def send_command(
    ser: serial.Serial, command: str, quiet_s: float = 0.35, max_s: float = 4.0
) -> tuple[serial.Serial, str]:
    for attempt in range(2):
        try:
            ser.write((command + "\r\n").encode("utf-8"))
            ser.flush()
            return read_until_quiet(ser, quiet_s=quiet_s, max_s=max_s)
        except serial.SerialException:
            ser = reopen_serial(ser)
            if attempt == 1:
                break
    return ser, ""


def render_prompt_audio(
    outdir: Path,
    phrase: str,
    rate: int,
    gain: float,
    repeat: int,
    voice: str | None,
) -> Path:
    say_path = outdir / "prompt.aiff"
    boosted_path = outdir / "prompt-boosted.wav"
    repeated_phrase = " ".join([phrase] * max(1, repeat))
    say_cmd = ["say"]
    if voice:
        say_cmd.extend(["-v", voice])
    say_cmd.extend(["-r", str(rate), "-o", str(say_path), repeated_phrase])
    subprocess.run(say_cmd, check=True)
    subprocess.run(
        [
            "ffmpeg",
            "-y",
            "-i",
            str(say_path),
            "-filter:a",
            f"volume={gain}",
            str(boosted_path),
        ],
        check=True,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    return boosted_path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="watch serial port")
    parser.add_argument("--turns", type=int, default=12, help="number of repeated turns to run")
    parser.add_argument("--phrase", default=DEFAULT_PHRASE)
    parser.add_argument("--device-mac", default=DEFAULT_DEVICE_MAC,
                        help="preferred watch MAC so the soak follows re-enumerated ports")
    parser.add_argument("--say-rate", type=int, default=165)
    parser.add_argument("--say-voice", default=None,
                        help="optional macOS say voice name, for example Samantha or Alex")
    parser.add_argument("--speaker-gain", type=float, default=3.0,
                        help="host playback gain multiplier applied via ffmpeg volume filter")
    parser.add_argument("--phrase-repeat", type=int, default=1,
                        help="number of times to repeat the spoken prompt in the rendered playback file")
    parser.add_argument("--pipeline-capture-ms", type=int, default=3000,
                        help="requested manual hold time for `pipeline capture` on the watch; 0 means open-ended")
    parser.add_argument("--no-host-audio", action="store_true",
                        help="skip macOS say/afplay prompt generation; useful with a mock voice endpoint")
    parser.add_argument("--pre-say-delay", type=float, default=0.5)
    parser.add_argument("--capture-start-timeout", type=float, default=12.0,
                        help="seconds to wait for the watch to report `manual capture start` before host playback")
    parser.add_argument("--turn-timeout", type=float, default=30.0)
    parser.add_argument("--settle-s", type=float, default=1.0)
    parser.add_argument("--restart-every", type=int, default=0,
                        help="issue `pipeline restart` before every Nth turn (0 disables)")
    parser.add_argument("--ready-internal-min", type=int, default=20000,
                        help="minimum connected internal heap before starting the measured soak")
    parser.add_argument("--min-internal", type=int, default=4096,
                        help="minimum internal heap allowed during measured pipeline stages")
    parser.add_argument("--min-largest", type=int, default=2048,
                        help="minimum largest internal block allowed during measured pipeline stages")
    parser.add_argument("--ready-timeout", type=float, default=45.0,
                        help="seconds to wait for connected heap to reach the ready threshold")
    parser.add_argument("--precreate-pipeline", action="store_true",
                        help="create/start the streaming pipeline before measured turns")
    parser.add_argument("--warmup-turns", type=int, default=0,
                        help="run this many unscored warmup capture/playback turns before measurement")
    args = parser.parse_args()

    if not args.no_host_audio:
        if shutil.which("say") is None:
            raise SystemExit("error: macOS `say` command not found")
        if shutil.which("ffmpeg") is None:
            raise SystemExit("error: ffmpeg command not found")
        if shutil.which("afplay") is None:
            raise SystemExit("error: afplay command not found")

    port = resolve_port(args.port, args.device_mac)
    stamp = time.strftime("%Y%m%d-%H%M%S")
    outdir = ROOT / "artifacts" / "qa" / f"voice-soak-{stamp}"
    outdir.mkdir(parents=True, exist_ok=True)

    results: list[dict[str, object]] = []
    raw_log: list[str] = []
    play_cmd: list[str] | None = None
    if not args.no_host_audio:
        playback_path = render_prompt_audio(
            outdir,
            args.phrase,
            args.say_rate,
            args.speaker_gain,
            args.phrase_repeat,
            args.say_voice,
        )
        play_cmd = ["afplay", str(playback_path)]

    with open_serial_quiet(port, 115200, timeout=0.2) as ser:
        time.sleep(0.8)
        ser.reset_input_buffer()
        ser, ready_text, ready_status = wait_for_connected_status(ser, args.ready_internal_min, args.ready_timeout)
        raw_log.append("\n=== ready status ===\n" + ready_text)
        if ready_status is None:
            raise SystemExit(
                "error: watch did not reach a connected ready state; "
                f"wanted internal>={args.ready_internal_min}"
            )

        if args.precreate_pipeline:
            ser, restart_text = send_command(ser, "pipeline restart", quiet_s=0.4, max_s=8.0)
            raw_log.append("\n=== precreate pipeline ===\n" + restart_text)
            time.sleep(max(args.settle_s, 1.0))
            ser, post_restart_status = send_command(ser, "pipeline status")
            raw_log.append("\n=== precreate pipeline status ===\n" + post_restart_status)

        total_turns = args.warmup_turns + args.turns
        for raw_turn in range(1, total_turns + 1):
            measured_turn = raw_turn - args.warmup_turns
            is_warmup = raw_turn <= args.warmup_turns
            if args.restart_every > 0 and raw_turn > 1 and (raw_turn - 1) % args.restart_every == 0:
                ser, restart_text = send_command(ser, "pipeline restart", quiet_s=0.4, max_s=8.0)
                raw_log.append(f"\n=== turn {raw_turn} restart ===\n{restart_text}")

            ser, pipeline_before_text = send_command(ser, "pipeline status")
            pipeline_before = parse_pipeline_status(pipeline_before_text)
            ser, before_text = send_command(ser, "qa status")
            before = parse_status(before_text)
            ser, before_tasks_text = send_command(ser, "qa tasks")
            before_tasks = parse_tasks(before_tasks_text)
            turn_label = f"warmup {raw_turn}" if is_warmup else f"turn {measured_turn}"
            raw_log.append(f"\n=== {turn_label} pipeline before ===\n{pipeline_before_text}")
            raw_log.append(f"\n=== {turn_label} before ===\n{before_text}")
            raw_log.append(f"\n=== {turn_label} before tasks ===\n{before_tasks_text}")

            capture_ms = 0 if args.pipeline_capture_ms == 0 else max(1500, min(15000, args.pipeline_capture_ms))
            ser, trigger_text = send_command(
                ser,
                f"pipeline capture {capture_ms}",
                quiet_s=0.2,
                max_s=12.0,
            )
            capture_ready_log = ""
            if "manual capture start" not in trigger_text:
                ser, capture_ready_log = read_until_any(
                    ser,
                    (
                        "manual capture start",
                        "voice fail",
                        "ESP_ERR_NO_MEM",
                        "Guru Meditation Error",
                        "TG1WDT_SYS_RST",
                    ),
                    timeout_s=args.capture_start_timeout,
                )
                if capture_ready_log:
                    trigger_text += capture_ready_log
            raw_log.append(f"\n=== {turn_label} trigger ===\n{trigger_text}")
            time.sleep(args.pre_say_delay)
            if play_cmd is not None:
                subprocess.run(play_cmd, check=True)

            ser, turn_log = read_until_any(
                ser,
                (
                    "[turn] done",
                    "No speech detected",
                    "voice-stream websocket error",
                    "voice fail",
                    "ESP_ERR_NO_MEM",
                    "TG1WDT_SYS_RST",
                    "Guru Meditation Error",
                ),
                timeout_s=args.turn_timeout,
            )
            if turn_needs_recovery_wait(turn_log):
                ser, recovery_log = read_until_any(
                    ser,
                    (
                        "[turn] done",
                        "No speech detected",
                        "response.done",
                        "[speak] ",
                        "TG1WDT_SYS_RST",
                        "Guru Meditation Error",
                        "ESP_ERR_NO_MEM",
                    ),
                    timeout_s=max(8.0, args.turn_timeout / 2.0),
                )
                if recovery_log:
                    turn_log += recovery_log
            if turn_needs_completion_wait(turn_log):
                ser, completion_log = read_until_any(
                    ser,
                    (
                        "[turn] done",
                        "TG1WDT_SYS_RST",
                        "Guru Meditation Error",
                        "ESP_ERR_NO_MEM",
                    ),
                    timeout_s=completion_wait_timeout(turn_log, args.turn_timeout),
                )
                if completion_log:
                    turn_log += completion_log
            ser, tail = read_until_quiet(ser, quiet_s=0.5, max_s=2.0)
            turn_log += tail
            raw_log.append(f"\n=== {turn_label} log ===\n{turn_log}")

            time.sleep(args.settle_s)
            ser, pipeline_after_text = send_command(ser, "pipeline status")
            pipeline_after = parse_pipeline_status(pipeline_after_text)
            ser, after_text = send_command(ser, "qa status")
            after = parse_status(after_text)
            ser, after_tasks_text = send_command(ser, "qa tasks")
            after_tasks = parse_tasks(after_tasks_text)
            raw_log.append(f"\n=== {turn_label} pipeline after ===\n{pipeline_after_text}")
            raw_log.append(f"\n=== {turn_label} after ===\n{after_text}")
            raw_log.append(f"\n=== {turn_label} after tasks ===\n{after_tasks_text}")

            transcript_match = re.findall(r"\[stt\]\s+(.+)", turn_log)
            reply_match = re.findall(r"\[reply\]\s+(.+)", turn_log)
            saw_success = "[turn] done" in turn_log
            saw_failure = turn_has_hard_failure(turn_log)
            ok = saw_success and not saw_failure
            result = {
                "turn": measured_turn if not is_warmup else 0,
                "raw_turn": raw_turn,
                "warmup": is_warmup,
                "ok": ok,
                "pipeline_before": pipeline_before,
                "pipeline_after": pipeline_after,
                "before": before,
                "after": after,
                "before_tasks": before_tasks,
                "after_tasks": after_tasks,
                "pipeline_heaps": parse_pipeline_heaps(turn_log),
                "transcript_lines": transcript_match[-2:] if transcript_match else [],
                "reply_lines": reply_match[-2:] if reply_match else [],
                "saw_turn_done": "[turn] done" in turn_log,
                "saw_response_done": "response.done" in turn_log,
                "saw_speaking": "[speak] " in turn_log,
                "saw_no_mem": "ESP_ERR_NO_MEM" in turn_log,
                "log_excerpt": turn_log[-1500:],
            }
            if is_warmup:
                raw_log.append(f"\n=== {turn_label} result ===\n{json.dumps(result, indent=2)}\n")
                print(
                    f"warmup {raw_turn}/{args.warmup_turns}: ok={ok} "
                    f"internal {before.get('internal') if before else '?'} -> {after.get('internal') if after else '?'}",
                    flush=True,
                )
                continue
            results.append(result)
            print(
                f"turn {measured_turn}/{args.turns}: ok={ok} "
                f"internal {before.get('internal') if before else '?'} -> {after.get('internal') if after else '?'} "
                f"largest {before.get('largest') if before else '?'} -> {after.get('largest') if after else '?'}",
                flush=True,
            )
            if not ok:
                break

    completed = len(results)
    internal_start = next((r["before"]["internal"] for r in results if r.get("before")), None)
    internal_end = results[-1]["after"]["internal"] if results and results[-1].get("after") else None
    largest_start = next((r["before"]["largest"] for r in results if r.get("before")), None)
    largest_end = results[-1]["after"]["largest"] if results and results[-1].get("after") else None
    min_internal_seen = min(
        [
            int(bucket["internal"])
            for result in results
            for bucket in ([result.get("before"), result.get("after")] + list(result.get("pipeline_heaps", [])))
            if isinstance(bucket, dict) and "internal" in bucket
        ],
        default=None,
    )
    min_largest_seen = min(
        [
            int(bucket["largest"])
            for result in results
            for bucket in ([result.get("before"), result.get("after")] + list(result.get("pipeline_heaps", [])))
            if isinstance(bucket, dict) and "largest" in bucket
        ],
        default=None,
    )
    failures = evaluate_failures(results, args.min_internal, args.min_largest)
    task_summary = summarize_task_peaks(results)
    summary = {
        "port": port,
        "device_mac": args.device_mac,
        "turns_requested": args.turns,
        "turns_completed": completed,
        "all_ok": completed == args.turns and all(bool(r["ok"]) for r in results) and not failures,
        "internal_start": internal_start,
        "internal_end": internal_end,
        "internal_delta": None if internal_start is None or internal_end is None else internal_end - internal_start,
        "largest_start": largest_start,
        "largest_end": largest_end,
        "largest_delta": None if largest_start is None or largest_end is None else largest_end - largest_start,
        "min_internal_seen": min_internal_seen,
        "min_largest_seen": min_largest_seen,
        "failures": failures,
        "task_summary": task_summary,
        "results": results,
    }

    (outdir / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    (outdir / "serial.log").write_text("".join(raw_log))
    print(f"voice soak artifacts: {outdir}")
    print(json.dumps({k: v for k, v in summary.items() if k != 'results'}, indent=2))
    return 0 if summary["all_ok"] else 2


if __name__ == "__main__":
    raise SystemExit(main())
