#!/usr/bin/env python3
"""Exercise every enabled LunaSay face through the authenticated Wi-Fi console.

The test deliberately uses the same post-recognition gesture queue as touch and
the same injected button event as the physical button.  It does not select
faces directly through ``POST /api/face``.
"""

from __future__ import annotations

import argparse
from datetime import datetime
import glob
import hashlib
import hmac
import json
from pathlib import Path
import re
import threading
import time
from urllib import error, request

import serial


ROOT = Path(__file__).resolve().parents[1]
PROVISION_RE = re.compile(
    r"device: provision mac=([0-9a-f:]{17}) secret=([0-9a-f]{64}) channel=(\S+)",
    re.I,
)


def resolve_port(value: str) -> str:
    if value:
        return value
    ports = sorted(glob.glob("/dev/cu.usbmodem*") + glob.glob("/dev/ttyACM*"))
    if len(ports) != 1:
        raise RuntimeError(f"expected one serial device, found {ports}")
    return ports[0]


def provision_console_credential(
    port: str, timeout: float = 40.0
) -> tuple[str, bytes, str, serial.Serial]:
    """Read the existing per-device credential without printing or persisting it."""
    deadline = time.monotonic() + timeout
    ser = serial.Serial(
        port,
        115200,
        timeout=0.25,
        write_timeout=1.0,
        exclusive=True,
    )
    try:
        ser.dtr = False
        ser.rts = False
        next_command = 0.0
        received = bytearray()
        while time.monotonic() < deadline:
            now = time.monotonic()
            if now >= next_command:
                ser.write(b"\ndevice provision\n")
                next_command = now + 2.0
            raw = ser.read(max(1, min(ser.in_waiting, 4096)))
            if not raw:
                continue
            received.extend(raw)
            if len(received) > 16384:
                del received[:-8192]
            match = PROVISION_RE.search(received.decode("utf-8", "replace"))
            if match:
                mac, secret_hex, channel = match.groups()
                return mac.lower(), bytes.fromhex(secret_hex), channel, ser
    except Exception:
        ser.close()
        raise
    ser.close()
    raise RuntimeError("device did not return its console credential")


class Device:
    def __init__(self, base_url: str, secret: bytes, http_timeout: float) -> None:
        self.base_url = base_url.rstrip("/")
        self.secret = secret
        self.http_timeout = http_timeout

    def _request_json(
        self,
        path: str,
        *,
        method: str = "GET",
        body: dict | None = None,
        headers: dict[str, str] | None = None,
        deadline_s: float = 45.0,
    ) -> dict:
        deadline = time.monotonic() + deadline_s
        last_error: Exception | None = None
        while time.monotonic() < deadline:
            data = json.dumps(body).encode() if body is not None else None
            req = request.Request(
                self.base_url + path,
                data=data,
                headers={
                    **({"Content-Type": "application/json"} if data is not None else {}),
                    **(headers or {}),
                },
                method=method,
            )
            try:
                with request.urlopen(req, timeout=self.http_timeout) as response:
                    raw = response.read().decode("utf-8", "replace")
                value = json.loads(raw)
                if not isinstance(value, dict):
                    raise ValueError("JSON response is not an object")
                return value
            except (error.URLError, TimeoutError, ValueError, json.JSONDecodeError) as exc:
                last_error = exc
                # A request may hit LunaSay's tiny wake listener. Give the full
                # HTTP server time to start before trying the API again.
                try:
                    request.urlopen(self.base_url + "/", timeout=1.5).close()
                except Exception:
                    pass
                time.sleep(0.75)
        raise RuntimeError(f"{method} {path} did not return JSON: {last_error}")

    def get(self, path: str, deadline_s: float = 90.0) -> dict:
        return self._request_json(path, deadline_s=deadline_s)

    def console(self, command: str) -> dict:
        challenge = self.get("/api/console/challenge")
        nonce = str(challenge["nonce"])
        mac = str(challenge["mac"])
        channel = str(challenge["channel"])
        payload = f"{mac}\n{nonce}\n{channel}\n".encode()
        signature = hmac.new(self.secret, payload, hashlib.sha256).hexdigest()
        return self._request_json(
            "/api/console",
            method="POST",
            body={"command": command},
            headers={
                "X-Astrolabe-Console-Nonce": nonce,
                "X-Astrolabe-Console-Signature": signature,
            },
            deadline_s=60.0,
        )

    def current_slug(self) -> str:
        return str(self.get("/api/face")["face"]["slug"])


def drain_serial(serial_monitor: serial.Serial, context: str) -> None:
    """Keep the remote-console task's USB log sink from applying backpressure."""
    chunks: list[bytes] = []
    while serial_monitor.in_waiting > 0:
        chunks.append(serial_monitor.read(min(serial_monitor.in_waiting, 4096)))
    if not chunks:
        return
    text = b"".join(chunks).decode("utf-8", "replace")
    if re.search(r"Guru Meditation|panic'ed|assert failed|Backtrace:", text, re.I):
        raise RuntimeError(f"firmware panic during {context}:\n{text[-6000:]}")


def console_with_serial(
    device: Device,
    serial_monitor: serial.Serial,
    command: str,
) -> dict:
    """Submit Wi-Fi control while continuously draining its CDC log sink."""
    result: dict | None = None
    failure: BaseException | None = None

    def submit() -> None:
        nonlocal result, failure
        try:
            result = device.console(command)
        except BaseException as exc:
            failure = exc

    worker = threading.Thread(target=submit, daemon=True)
    worker.start()
    while worker.is_alive():
        drain_serial(serial_monitor, command)
        worker.join(0.05)
    drain_serial(serial_monitor, command)
    if failure is not None:
        raise failure
    if result is None:
        raise RuntimeError(f"console command returned no result: {command}")
    return result


def wait_for_slug(
    device: Device,
    serial_monitor: serial.Serial,
    expected: str,
    timeout: float,
) -> tuple[bool, str]:
    deadline = time.monotonic() + timeout
    last = ""
    while time.monotonic() < deadline:
        drain_serial(serial_monitor, f"swipe to {expected}")
        try:
            last = device.current_slug()
            if last == expected:
                drain_serial(serial_monitor, f"swipe to {expected}")
                return True, last
        except Exception:
            pass
        time.sleep(0.35)
    return False, last


def face_tts(device: Device, serial_monitor: serial.Serial, slug: str, timeout: float) -> dict:
    before = device.get("/api/voice").get("face_tts", {})
    before_sequence = int(before.get("sequence", 0))
    serial_monitor.reset_input_buffer()
    accepted = console_with_serial(device, serial_monitor, "button tts")
    if not accepted.get("accepted"):
        raise RuntimeError(f"button press rejected on {slug}: {accepted}")

    playback: dict | None = None
    playback_observed_at: float | None = None
    panic_observed_at: float | None = None
    panic_lines: list[str] = []
    serial_lines: list[str] = []
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            raw = serial_monitor.readline()
        except serial.SerialException as exc:
            tail = "\n".join(panic_lines if panic_lines else serial_lines[-20:])
            raise RuntimeError(f"serial disconnected during {slug} TTS: {exc}\n{tail}") from exc
        if not raw:
            if panic_observed_at is not None and time.monotonic() - panic_observed_at >= 5.0:
                tail = "\n".join(panic_lines)
                raise RuntimeError(f"firmware panic during {slug} TTS:\n{tail}")
            # Several firmware faults surfaced shortly after the codec
            # reported playback complete. Keep the passive witness open long
            # enough to capture delayed task panics before querying telemetry.
            if playback_observed_at is not None and time.monotonic() - playback_observed_at >= 8.0:
                break
            continue
        line = raw.decode("utf-8", "replace").strip()
        serial_lines.append(line)
        if panic_observed_at is not None and len(panic_lines) < 160:
            panic_lines.append(line)
        if len(serial_lines) > 240:
            del serial_lines[:-200]
        if re.search(r"Guru Meditation|panic'ed|assert failed|Backtrace:", line, re.I):
            # Keep draining the UART after the first panic marker so the
            # register dump and complete backtrace survive in the QA report.
            # Raising immediately here leaves only "Guru Meditation", which
            # is insufficient to map the fault to a source line.
            if panic_observed_at is None:
                panic_observed_at = time.monotonic()
                panic_lines = serial_lines[-40:].copy()
        play_match = re.search(r"\[tts\] play file done in (\d+)ms err=(\S+)", line)
        if play_match:
            playback = {
                "elapsed_ms": int(play_match.group(1)),
                "err": play_match.group(2),
            }
            if playback["err"] != "ESP_OK":
                raise RuntimeError(f"audio playback failed on {slug}: {playback['err']}")
            playback_observed_at = time.monotonic()
        done_match = re.search(r"tts-face: done slug=(\S+) err=(\S+)", line)
        if done_match and done_match.group(1) == slug and done_match.group(2) != "ESP_OK":
            tail = "\n".join(serial_lines[-40:])
            raise RuntimeError(
                f"TTS failed on {slug}: {done_match.group(2)}\n{tail}"
            )
    if playback is None:
        raise RuntimeError(f"TTS/audio timed out on {slug}")

    # The full HTTP server is intentionally absent during cloud/audio work.
    # Only wake it after serial confirms that synchronous playback completed.
    time.sleep(0.5)
    status = device.get("/api/voice", deadline_s=120.0).get("face_tts", {})
    if (
        int(status.get("sequence", 0)) <= before_sequence
        or str(status.get("slug", "")) != slug
        or bool(status.get("busy", False))
        or int(status.get("completed_ms", 0)) <= 0
        or status.get("err") != "ESP_OK"
    ):
        tail = "\n".join(serial_lines[-30:])
        raise RuntimeError(f"invalid completion status on {slug}: {status}\n{tail}")
    status["serial_playback"] = playback
    status["serial_evidence"] = [
        line
        for line in serial_lines
        if any(
            marker in line
            for marker in ("tts-face", "[voice]", "[tts]", "voice: HTTP", "voice: message")
        )
    ][-20:]
    return status


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--url", default="http://192.168.86.50")
    parser.add_argument("--port", default="")
    parser.add_argument("--http-timeout", type=float, default=15.0)
    parser.add_argument("--tts-timeout", type=float, default=180.0)
    parser.add_argument("--swipe-timeout", type=float, default=18.0)
    parser.add_argument("--boot-settle", type=float, default=25.0)
    parser.add_argument("--out-dir", type=Path)
    args = parser.parse_args()

    started = datetime.now().astimezone()
    out_dir = args.out_dir or (
        ROOT / "artifacts" / "qa" / f"lunasay-wifi-tts-tour-{started:%Y%m%d-%H%M%S}"
    )
    out_dir.mkdir(parents=True, exist_ok=True)
    report: dict = {
        "schema": 1,
        "started_at": started.isoformat(),
        "url": args.url,
        "port": resolve_port(args.port),
        "faces": [],
        "transitions": [],
        "passed": False,
    }

    serial_monitor: serial.Serial | None = None
    try:
        mac, secret, provision_channel, serial_monitor = provision_console_credential(report["port"])
        print(f"Console credential loaded for {mac}; secret retained in memory only.", flush=True)
        # Lock before the 20-second automatic OTA poll. Wi-Fi association can
        # take longer than that at nightstand signal levels, so this setup-only
        # guard uses the already-open CDC console. All tour controls below use
        # the authenticated Wi-Fi console.
        serial_monitor.write(b"\nota test-lock on\n")
        device = Device(args.url, secret, args.http_timeout)
        settings = device.get("/api/settings", deadline_s=75.0)
        lock_applied = bool(settings.get("ota", {}).get("testLocked", False))
        if not lock_applied:
            raise RuntimeError("automatic OTA lock was not applied")
        print("Automatic OTA locked for this non-persistent test session.", flush=True)
        print(f"Waiting {args.boot_settle:.0f}s for display, touch, and audio readiness.", flush=True)
        settle_deadline = time.monotonic() + args.boot_settle
        while time.monotonic() < settle_deadline:
            drain_serial(serial_monitor, "boot settle")
            time.sleep(0.05)

        inventory = device.get("/api/faces", deadline_s=60.0)
        enabled = sorted(
            (
                face
                for face in inventory.get("faces", [])
                if face.get("enabled") and face.get("nav") and face.get("ported")
            ),
            key=lambda face: int(face["order"]),
        )
        if not enabled:
            raise RuntimeError("no enabled navigation faces reported")
        slugs = [str(face["slug"]) for face in enabled]
        current = device.current_slug()
        profile_deadline = time.monotonic() + 30.0
        while current not in slugs and time.monotonic() < profile_deadline:
            # The HTTP server can become available just before the LunaSay
            # profile restores its persisted/default face after a USB reset.
            time.sleep(0.5)
            current = device.current_slug()
        if current not in slugs:
            raise RuntimeError(f"current face {current!r} is outside enabled navigation")
        start_index = slugs.index(current)
        tour = slugs[start_index:] + slugs[:start_index]

        initial_battery = device.get("/api/battery")
        report.update(
            {
                "device_mac": mac,
                "channel": provision_channel,
                "firmware": initial_battery.get("firmware", {}),
                "enabled_faces": slugs,
                "tour_order": tour,
                "initial_uptime_ms": initial_battery.get("uptime_ms"),
                "initial_reset_reason": initial_battery.get("reset_reason"),
            }
        )
        print(f"Tour order ({len(tour)}): {' -> '.join(tour)}", flush=True)

        previous_uptime = int(initial_battery.get("uptime_ms", 0))
        for index, slug in enumerate(tour):
            actual = device.current_slug()
            if actual != slug:
                raise RuntimeError(f"expected face {slug}, device reports {actual}")
            print(f"[{index + 1}/{len(tour)}] {slug}: simulated TTS press", flush=True)
            tts_status: dict | None = None
            for tts_attempt in range(1, 4):
                try:
                    tts_status = face_tts(
                        device, serial_monitor, slug, args.tts_timeout
                    )
                    tts_status["attempts"] = tts_attempt
                    break
                except RuntimeError as exc:
                    recoverable = (
                        f"TTS failed on {slug}: ESP_FAIL" in str(exc)
                        and tts_attempt < 3
                    )
                    if not recoverable:
                        raise
                    print(
                        f"[{index + 1}/{len(tour)}] {slug}: "
                        f"transient service failure; retrying "
                        f"({tts_attempt + 1}/3)",
                        flush=True,
                    )
                    # A failed cloud turn can leave the tiny wake listener in
                    # front of the full server. Give both tasks a bounded
                    # recovery interval before requesting the next turn.
                    retry_deadline = time.monotonic() + 5.0
                    while time.monotonic() < retry_deadline:
                        drain_serial(serial_monitor, f"{slug} TTS retry")
                        time.sleep(0.05)
            if tts_status is None:
                raise RuntimeError(f"TTS produced no result on {slug}")
            battery = device.get("/api/battery")
            uptime = int(battery.get("uptime_ms", 0))
            if uptime < previous_uptime:
                raise RuntimeError(f"device reset while testing {slug}")
            previous_uptime = uptime
            if device.current_slug() != slug:
                raise RuntimeError(f"face changed during TTS on {slug}")
            report["faces"].append(
                {
                    "slug": slug,
                    "tts": tts_status,
                    "uptime_ms": uptime,
                    "reset_reason": battery.get("reset_reason"),
                }
            )
            print(
                f"[{index + 1}/{len(tour)}] {slug}: TTS complete "
                f"({tts_status.get('elapsed_ms')} ms)",
                flush=True,
            )

            expected = tour[(index + 1) % len(tour)]
            transition_started = time.monotonic()
            drain_serial(serial_monitor, f"before swipe from {slug}")
            accepted = console_with_serial(device, serial_monitor, "gesture swipe right")
            if not accepted.get("accepted"):
                raise RuntimeError(f"swipe rejected after {slug}: {accepted}")
            changed, actual = wait_for_slug(device, serial_monitor, expected, args.swipe_timeout)
            elapsed_ms = round((time.monotonic() - transition_started) * 1000)
            report["transitions"].append(
                {
                    "from": slug,
                    "to": expected,
                    "actual": actual,
                    "elapsed_ms": elapsed_ms,
                    "passed": changed,
                }
            )
            if not changed:
                raise RuntimeError(f"right swipe {slug} -> {expected} failed; current={actual}")
            print(f"  swipe right -> {expected} ({elapsed_ms} ms)", flush=True)

        final_battery = device.get("/api/battery")
        final_uptime = int(final_battery.get("uptime_ms", 0))
        if final_uptime < previous_uptime:
            raise RuntimeError("device reset at the end of the tour")
        report.update(
            {
                "final_uptime_ms": final_uptime,
                "final_reset_reason": final_battery.get("reset_reason"),
                "finished_at": datetime.now().astimezone().isoformat(),
                "passed": True,
            }
        )
        print("PASS: every face spoke and every injected swipe completed.", flush=True)
        return 0
    except Exception as exc:
        report["error"] = str(exc)
        report["finished_at"] = datetime.now().astimezone().isoformat()
        print(f"FAIL: {exc}", flush=True)
        return 1
    finally:
        if serial_monitor is not None:
            serial_monitor.close()
        report_path = out_dir / "report.json"
        report_path.write_text(json.dumps(report, indent=2) + "\n")
        print(f"Report: {report_path}", flush=True)


if __name__ == "__main__":
    raise SystemExit(main())
