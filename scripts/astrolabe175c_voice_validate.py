#!/usr/bin/env python3
"""End-to-end Astrolabe Faculty voice smoke test.

The test uses macOS `say` as a known acoustic STT prompt and records the
watch's TTS response with the Mac microphone. It leaves timestamped logs and
audio in artifacts/qa/voice-e2e-*.
"""

from __future__ import annotations

import argparse
import glob
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

import serial


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_PHRASE = "Astrolabe voice test. The sky is clear over Castalia."


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
    preferred = "/dev/cu.usbmodem11301"
    if Path(preferred).exists():
        return preferred
    ports = sorted(glob.glob("/dev/cu.usbmodem*"))
    if len(ports) == 1:
        return ports[0]
    if not ports:
        raise SystemExit("error: no Astrolabe serial port found (/dev/cu.usbmodem*)")
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


def run_checked(cmd: list[str], **kwargs: object) -> subprocess.CompletedProcess[str]:
    print("+", " ".join(cmd), flush=True)
    return subprocess.run(cmd, text=True, check=True, **kwargs)


def record_mic(path: Path, seconds: float, audio_device: str) -> subprocess.Popen[str] | None:
    ffmpeg = shutil.which("ffmpeg")
    if ffmpeg is None:
        print("warning: ffmpeg not found; skipping Mac mic recording", file=sys.stderr)
        return None
    cmd = [
        ffmpeg,
        "-hide_banner",
        "-loglevel",
        "warning",
        "-y",
        "-f",
        "avfoundation",
        "-i",
        f":{audio_device}",
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


def host_audio_self_test(outdir: Path, audio_device: str) -> bool:
    say_path = outdir / "say-test.aiff"
    run_checked(["say", "-o", str(say_path), "Astrolabe host audio test."])
    recorder = record_mic(outdir / "mic-test.wav", 1.0, audio_device)
    if recorder is None:
        (outdir / "host-audio.txt").write_text("ffmpeg missing; microphone capture skipped\n")
        return False
    ok, log = finish_recorder(recorder, 4.0)
    mic_path = outdir / "mic-test.wav"
    mic_ok = ok and mic_path.exists() and mic_path.stat().st_size > 1024
    (outdir / "host-audio.txt").write_text(
        f"say_file={say_path} say_size={say_path.stat().st_size if say_path.exists() else 0}\n"
        f"mic_file={mic_path} mic_size={mic_path.stat().st_size if mic_path.exists() else 0}\n"
        f"mic_ok={mic_ok}\n\n{log}"
    )
    return mic_ok


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="watch serial port")
    parser.add_argument("--phrase", default=DEFAULT_PHRASE)
    parser.add_argument("--stt-ms", type=int, default=7000)
    parser.add_argument("--tts-record-s", type=float, default=8.0)
    parser.add_argument("--audio-device", default=os.environ.get("ASTROLABE175C_MAC_AUDIO_DEVICE", "1"),
                        help="ffmpeg avfoundation audio device index; default 1 (MacBook Pro Microphone)")
    parser.add_argument("--dry-run", action="store_true", help="check host tools and relay without opening serial")
    parser.add_argument("--host-audio-test", action="store_true", help="also test macOS say file output and mic capture")
    args = parser.parse_args()

    say = shutil.which("say")
    if say is None:
        raise SystemExit("error: macOS say command not found")
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
            audio_ok = host_audio_self_test(outdir, args.audio_device)
        print(f"astrolabe175c_voice_validate: dry-run ok outdir={outdir} host_audio_ok={audio_ok}")
        return 0 if audio_ok else 3

    port = resolve_port(args.port)
    log_parts: list[str] = []
    with serial.Serial(port, 115200, timeout=0.2) as ser:
        time.sleep(0.8)
        ser.reset_input_buffer()
        ser.write(f"stt {args.stt_ms}\r\n".encode("utf-8"))
        first = read_until(ser, ("qa: stt capture begin", "stt: capture_ms="), 5.0)
        log_parts.append(first)
        run_checked([say, args.phrase])
        stt_log = read_until(ser, ("qa: stt done",), max(12.0, args.stt_ms / 1000.0 + 8.0))
        log_parts.append(stt_log)

        tts_audio = outdir / "tts-mac-mic.wav"
        recorder = record_mic(tts_audio, args.tts_record_s, args.audio_device)
        time.sleep(0.5)
        ser.write(b"tts face\r\n")
        tts_log = read_until(ser, ("tts: face", "speak", "tts"), args.tts_record_s + 5.0)
        log_parts.append(tts_log)
        if recorder is not None:
            recorder_ok, recorder_log = finish_recorder(recorder, args.tts_record_s + 5.0)
            (outdir / "ffmpeg.txt").write_text(f"recorder_ok={recorder_ok}\n{recorder_log}")

        log_parts.append(read_for(ser, 1.0))

    serial_log = "".join(log_parts)
    (outdir / "serial.log").write_text(serial_log)

    stt_ok = "qa: stt transcript=" in serial_log and "qa: stt done err=ESP_OK" in serial_log
    tts_ok = "tts: face ESP_OK" in serial_log or "voice tts" in serial_log
    print(f"astrolabe175c_voice_validate: outdir={outdir}")
    print(f"astrolabe175c_voice_validate: stt_ok={stt_ok} tts_trigger_ok={tts_ok}")
    if not stt_ok or not tts_ok:
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
