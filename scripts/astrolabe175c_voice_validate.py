#!/usr/bin/env python3
"""End-to-end Astrolabe Faculty voice smoke test.

The test uses host text-to-speech as a known acoustic STT prompt and records
serial evidence that the watch loads the Faculty face, routes the spoken
request to the requested faculty, loads that faculty's face, and speaks a
reply. It leaves timestamped logs and optional audio in artifacts/qa/voice-e2e-*.
"""

from __future__ import annotations

import argparse
import glob
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time

import serial


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_FACULTY_SLUG = "a.einstein"
DEFAULT_FACULTY_NAME = "Einstein"
DEFAULT_RESET_SLUG = "a.darwin"
DEFAULT_RESET_NAME = "Charles Darwin"
DEFAULT_PHRASE = "Ask Einstein: what is one precise way to test a small machine?"


def resolve_port(port: str | None) -> str:
    if port:
        return port
    for name in (
        "ASTROLABE175C_VOICE_PORT",
        "ASTROLABE175C_TIME_PORT",
        "ASTROLABE_UPLOAD_PORT",
        "UPLOAD_PORT",
        "ESPPORT",
        "IDF_PORT",
    ):
        value = os.environ.get(name)
        if value:
            return value
    preferred = (
        "/dev/cu.usbmodem11301",
        "/dev/ttyACM0",
        "/dev/ttyUSB0",
    )
    for candidate in preferred:
        if Path(candidate).exists():
            return candidate
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


def read_for(ser: serial.Serial, seconds: float) -> str:
    end = time.time() + seconds
    chunks: list[bytes] = []
    while time.time() < end:
        chunk = ser.read(4096)
        if chunk:
            chunks.append(chunk)
    return b"".join(chunks).decode("utf-8", "replace")


def read_until(ser: serial.Serial, needles: tuple[str, ...], timeout_s: float) -> str:
    end = time.time() + timeout_s
    chunks: list[bytes] = []
    text = ""
    while time.time() < end:
        chunk = ser.read(4096)
        if chunk:
            chunks.append(chunk)
            text = b"".join(chunks).decode("utf-8", "replace")
            if any(needle in text for needle in needles):
                return text
    return text


def read_until_all(ser: serial.Serial, needles: tuple[str, ...], timeout_s: float) -> str:
    end = time.time() + timeout_s
    chunks: list[bytes] = []
    text = ""
    while time.time() < end:
        chunk = ser.read(4096)
        if chunk:
            chunks.append(chunk)
            text = b"".join(chunks).decode("utf-8", "replace")
            if all(needle in text for needle in needles):
                return text
    return text


def run_checked(cmd: list[str], **kwargs: object) -> subprocess.CompletedProcess[str]:
    print("+", " ".join(cmd), flush=True)
    return subprocess.run(cmd, text=True, check=True, **kwargs)


def host_speech_command(phrase: str) -> list[str]:
    say = shutil.which("say")
    if say is not None:
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


def record_mic(path: Path, seconds: float, audio_device: str, audio_backend: str) -> subprocess.Popen[str] | None:
    ffmpeg = shutil.which("ffmpeg")
    if ffmpeg is None:
        print("warning: ffmpeg not found; skipping host mic recording", file=sys.stderr)
        return None
    if audio_backend == "avfoundation":
        input_args = ["-f", "avfoundation", "-i", f":{audio_device}"]
    elif audio_backend == "pulse":
        input_args = ["-f", "pulse", "-i", audio_device]
    elif audio_backend == "alsa":
        input_args = ["-f", "alsa", "-i", audio_device]
    else:
        raise SystemExit(f"error: unknown audio backend {audio_backend!r}")
    cmd = [
        ffmpeg,
        "-hide_banner",
        "-loglevel",
        "warning",
        "-y",
        *input_args,
        "-t",
        f"{seconds:.2f}",
        str(path),
    ]
    print("+", " ".join(cmd), flush=True)
    return subprocess.Popen(cmd, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)


def finish_recorder(recorder: subprocess.Popen[str], timeout_s: float) -> tuple[bool, str]:
    try:
        stdout, stderr = recorder.communicate(timeout=timeout_s)
        return recorder.returncode == 0, (stdout or "") + (stderr or "")
    except subprocess.TimeoutExpired:
        recorder.kill()
        stdout, stderr = recorder.communicate(timeout=2.0)
        return False, (stdout or "") + (stderr or "") + "\nrecorder timeout; killed\n"


def host_audio_self_test(outdir: Path, audio_device: str, audio_backend: str) -> bool:
    speech_cmd = host_speech_command("Astrolabe host audio test.")
    recorder = record_mic(outdir / "mic-test.wav", 1.0, audio_device, audio_backend)
    run_checked(speech_cmd)
    if recorder is None:
        (outdir / "host-audio.txt").write_text("ffmpeg missing; microphone capture skipped\n")
        return False
    ok, log = finish_recorder(recorder, 4.0)
    mic_path = outdir / "mic-test.wav"
    mic_ok = ok and mic_path.exists() and mic_path.stat().st_size > 1024
    (outdir / "host-audio.txt").write_text(
        f"speech_cmd={' '.join(speech_cmd)}\n"
        f"mic_file={mic_path} mic_size={mic_path.stat().st_size if mic_path.exists() else 0}\n"
        f"mic_ok={mic_ok}\n\n{log}"
    )
    return mic_ok


def write_cmd(ser: serial.Serial, cmd: str) -> None:
    print(f"> {cmd}", flush=True)
    ser.write((cmd + "\r\n").encode("utf-8"))


def issue_cmd(ser: serial.Serial, cmd: str, needles: tuple[str, ...], timeout_s: float) -> str:
    write_cmd(ser, cmd)
    return read_until(ser, needles, timeout_s)


def ui_reports_faculty(text: str, slug: str, ready: bool = False) -> bool:
    slug_ok = re.search(rf"\bslug={re.escape(slug)}\b", text) is not None
    bust_ok = "bust=ready" in text if ready else ("bust=ready" in text or "bust=loading" in text)
    return slug_ok and bust_ok


def wait_for_faculty_ui(ser: serial.Serial, slug: str, timeout_s: float) -> str:
    end = time.time() + timeout_s
    chunks: list[str] = []
    while time.time() < end:
        ui_log = issue_cmd(ser, "qa ui", ("qa: ui=",), 3.0)
        chunks.append(ui_log)
        if ui_reports_faculty(ui_log, slug, ready=True):
            break
        time.sleep(1.0)
    return "".join(chunks)


def stage_seen(serial_log: str, stage: str) -> bool:
    return re.search(rf"\[{re.escape(stage)}\]", serial_log) is not None


def stage_mentions(serial_log: str, stage: str, text: str) -> bool:
    if not text:
        return False
    return re.search(rf"\[{re.escape(stage)}\][^\n]*{re.escape(text)}", serial_log, re.IGNORECASE) is not None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="watch serial port")
    parser.add_argument("--phrase", default=DEFAULT_PHRASE)
    parser.add_argument("--faculty-slug", default=DEFAULT_FACULTY_SLUG)
    parser.add_argument("--faculty-name", default=DEFAULT_FACULTY_NAME)
    parser.add_argument("--reset-slug", default=DEFAULT_RESET_SLUG)
    parser.add_argument("--reset-name", default=DEFAULT_RESET_NAME)
    parser.add_argument("--stt-ms", type=int, default=9000)
    parser.add_argument("--tts-record-s", type=float, default=8.0)
    parser.add_argument("--audio-device", default=os.environ.get("ASTROLABE175C_AUDIO_DEVICE", "default"),
                        help="ffmpeg audio input device; default PulseAudio device is 'default'")
    parser.add_argument("--audio-backend", default=os.environ.get("ASTROLABE175C_AUDIO_BACKEND", "pulse"),
                        choices=("pulse", "alsa", "avfoundation"))
    parser.add_argument("--dry-run", action="store_true", help="check host tools and relay without opening serial")
    parser.add_argument("--host-audio-test", action="store_true", help="also test host TTS output and mic capture")
    args = parser.parse_args()

    speech_cmd = host_speech_command(args.phrase)
    if shutil.which("ffmpeg") is None:
        print("warning: ffmpeg not found; TTS audio recording will be skipped", file=sys.stderr)

    stamp = time.strftime("%Y%m%d-%H%M%S")
    outdir = ROOT / "artifacts" / "qa" / f"voice-e2e-{stamp}"
    outdir.mkdir(parents=True, exist_ok=True)

    relay = subprocess.run(["curl", "-sS", "--max-time", "2", "http://127.0.0.1:8787/"],
                           text=True, capture_output=True)
    (outdir / "relay.txt").write_text(relay.stdout + relay.stderr)
    if "astrolabe voice relay ok" not in relay.stdout:
        print("warning: local voice relay did not answer on 127.0.0.1:8787", file=sys.stderr)

    if args.dry_run:
        audio_ok = True
        if args.host_audio_test:
            audio_ok = host_audio_self_test(outdir, args.audio_device, args.audio_backend)
        print(f"astrolabe175c_voice_validate: dry-run ok outdir={outdir} host_audio_ok={audio_ok}")
        return 0 if audio_ok else 3

    port = resolve_port(args.port)
    log_parts: list[str] = []
    with serial.Serial(port, 115200, timeout=0.2) as ser:
        time.sleep(0.8)
        ser.reset_input_buffer()
        log_parts.append(issue_cmd(ser, "faces set faculty", ("faces: set faculty", "ESP_OK"), 5.0))
        log_parts.append(issue_cmd(
            ser,
            f"qa faculty-fetch {args.reset_slug} {args.reset_name}",
            ("qa: faculty=", "bust="),
            8.0,
        ))
        log_parts.append(issue_cmd(ser, "qa ui", ("qa: ui=",), 3.0))

        tts_audio = outdir / "faculty-reply-host-mic.wav"
        recorder = record_mic(tts_audio, args.tts_record_s, args.audio_device, args.audio_backend)
        write_cmd(ser, "button press")
        first = read_until(ser, ("button: inject ESP_OK", "faculty button STT", "capture"), 5.0)
        log_parts.append(first)
        run_checked(speech_cmd)
        turn_log = read_until_all(
            ser,
            ("[stt]", "[reply]", "[speak]", "[turn] done"),
            max(20.0, args.stt_ms / 1000.0 + args.tts_record_s + 20.0),
        )
        log_parts.append(turn_log)
        if recorder is not None:
            recorder_ok, recorder_log = finish_recorder(recorder, args.tts_record_s + 8.0)
            (outdir / "ffmpeg.txt").write_text(f"recorder_ok={recorder_ok}\n{recorder_log}")

        log_parts.append(wait_for_faculty_ui(ser, args.faculty_slug, 30.0))
        log_parts.append(read_for(ser, 1.0))

    serial_log = "".join(log_parts)
    (outdir / "serial.log").write_text(serial_log)

    face_ok = "faces: set faculty" in serial_log and "ESP_OK" in serial_log
    stt_ok = stage_seen(serial_log, "stt")
    reply_ok = (
        stage_seen(serial_log, "reply")
        and stage_mentions(serial_log, "speak", args.faculty_name)
        and "[turn] done" in serial_log
    )
    faculty_ok = ui_reports_faculty(serial_log, args.faculty_slug, ready=False)
    bust_ok = ui_reports_faculty(serial_log, args.faculty_slug, ready=True)
    print(f"astrolabe175c_voice_validate: outdir={outdir}")
    print(
        "astrolabe175c_voice_validate: "
        f"face_ok={face_ok} stt_ok={stt_ok} reply_ok={reply_ok} "
        f"faculty_ok={faculty_ok} bust_ok={bust_ok}"
    )
    if not face_ok or not stt_ok or not reply_ok or not faculty_ok or not bust_ok:
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
