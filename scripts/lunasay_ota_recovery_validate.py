#!/usr/bin/env python3
"""Validate LunaSay OTA integrity rejection and factory/product recovery.

This is a local hardware test, not a substitute for the production signed-
manifest gate. It serves the already-built LunaSay image over the LAN, proves
that an incorrect SHA-256 is rejected, installs the same image with its correct
SHA-256, and exercises serial-commanded factory and product boot recovery.
"""

from __future__ import annotations

import argparse
from datetime import datetime
import glob
import hashlib
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import re
import socket
import struct
import threading
import time
from urllib import request

import serial
from serial import SerialException


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_IMAGE = ROOT / "astrolabe175c" / "build" / "astrolabe175c.bin"
STATUS_RE = re.compile(r"ota: status .*running=(\S+) boot=(\S+)")
CRASH_RE = re.compile(r"Guru Meditation|assert failed|CORRUPT HEAP|panic'ed", re.I)
ESP_APP_DESC_MAGIC = 0xABCD5432


def sha256_hex(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def image_app_identity(path: Path) -> dict:
    """Read the first ESP app descriptor without importing the IDF toolchain."""
    with path.open("rb") as handle:
        header = handle.read(24)
        segment_header = handle.read(8)
        descriptor = handle.read(176)
    if len(header) != 24 or len(segment_header) != 8 or len(descriptor) != 176:
        raise ValueError(f"firmware image is too short for an ESP app descriptor: {path}")
    magic = struct.unpack_from("<I", descriptor, 0)[0]
    if magic != ESP_APP_DESC_MAGIC:
        raise ValueError(f"firmware app descriptor magic is invalid: 0x{magic:08x}")
    decode = lambda data: data.split(b"\0", 1)[0].decode("utf-8", "strict")
    version = decode(descriptor[16:48])
    project = decode(descriptor[48:80])
    elf_sha256 = descriptor[144:176].hex()
    if not version or not project or len(elf_sha256) != 64:
        raise ValueError("firmware app descriptor identity is incomplete")
    return {"project": project, "version": version, "elf_sha256": elf_sha256}


def battery_firmware_identity(device_ip: str) -> dict:
    with request.urlopen(f"http://{device_ip}/api/battery", timeout=5.0) as response:
        payload = json.load(response)
    firmware = payload.get("firmware", {})
    return {
        "project": firmware.get("project"),
        "version": firmware.get("version"),
        "variant": firmware.get("variant"),
        "elf_sha256": firmware.get("elf_sha256"),
    }


def resolve_port(requested: str) -> str:
    if requested:
        return requested
    ports = sorted(glob.glob("/dev/cu.usbmodem*") + glob.glob("/dev/ttyACM*"))
    if len(ports) != 1:
        raise SystemExit(f"error: expected one serial port, found {ports}; pass --port")
    return ports[0]


def resolve_lan_ip(requested: str) -> str:
    if requested:
        return requested
    # UDP connect selects the interface without sending application data.
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        sock.connect(("1.1.1.1", 80))
        address = sock.getsockname()[0]
    finally:
        sock.close()
    if address.startswith("127.") or address == "0.0.0.0":
        raise SystemExit("error: could not determine a LAN address; pass --host")
    return address


class QuietHandler(SimpleHTTPRequestHandler):
    def log_message(self, format: str, *args: object) -> None:
        return


def start_server(image: Path, host: str, port: int) -> tuple[ThreadingHTTPServer, str]:
    class FirmwareHandler(QuietHandler):
        def do_GET(self) -> None:  # noqa: N802 - stdlib handler API
            if self.path != "/firmware.bin":
                self.send_error(404)
                return
            self.send_response(200)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Content-Length", str(image.stat().st_size))
            self.end_headers()
            with image.open("rb") as handle:
                while chunk := handle.read(64 * 1024):
                    self.wfile.write(chunk)

    server = ThreadingHTTPServer(("0.0.0.0", port), FirmwareHandler)
    thread = threading.Thread(target=server.serve_forever, name="ota-http", daemon=True)
    thread.start()
    actual_port = int(server.server_address[1])
    return server, f"http://{host}:{actual_port}/firmware.bin"


def wait_for_device_http(device_ip: str, timeout: float = 60.0) -> None:
    deadline = time.monotonic() + timeout
    last_error: Exception | None = None
    while time.monotonic() < deadline:
        try:
            with request.urlopen(f"http://{device_ip}/", timeout=2.0) as response:
                response.read(256)
            return
        except Exception as exc:
            last_error = exc
            time.sleep(1.0)
    raise RuntimeError(f"device network did not become ready at {device_ip}: {last_error}")


class DeviceConsole:
    def __init__(self, port: str, transcript: list[str]) -> None:
        self.port = port
        self.transcript = transcript
        self.ser: serial.Serial | None = None

    def close(self) -> None:
        if self.ser is not None:
            try:
                self.ser.close()
            except SerialException:
                pass
            self.ser = None

    def connect(self, timeout: float = 45.0) -> None:
        self.close()
        deadline = time.monotonic() + timeout
        last_error: Exception | None = None
        while time.monotonic() < deadline:
            try:
                ser = serial.Serial(self.port, 115200, timeout=0.15)
                ser.dtr = False
                ser.rts = False
                self.ser = ser
                time.sleep(0.5)
                return
            except (OSError, SerialException) as exc:
                last_error = exc
                time.sleep(0.5)
        raise RuntimeError(f"serial did not reconnect on {self.port}: {last_error}")

    def command(self, command: str, patterns: tuple[str, ...], timeout: float) -> str:
        if self.ser is None:
            self.connect()
        assert self.ser is not None
        self.transcript.append(f">>> {command}")
        try:
            self.ser.write((command + "\r\n").encode())
            self.ser.flush()
        except (OSError, SerialException):
            self.connect()
            assert self.ser is not None
            self.ser.write((command + "\r\n").encode())
            self.ser.flush()

        rows: list[str] = []
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                raw = self.ser.readline()
            except (OSError, SerialException):
                break
            if not raw:
                continue
            row = raw.decode("utf-8", "replace").rstrip()
            if row:
                rows.append(row)
                self.transcript.append(row)
                text = "\n".join(rows)
                if any(pattern in text for pattern in patterns):
                    return text
        return "\n".join(rows)

    def status(self, expected_running: str, timeout: float = 60.0) -> tuple[bool, str]:
        deadline = time.monotonic() + timeout
        combined: list[str] = []
        while time.monotonic() < deadline:
            try:
                self.connect(timeout=min(8.0, max(1.0, deadline - time.monotonic())))
                text = self.command("ota status", ("ota: status",), 4.0)
                combined.append(text)
                match = STATUS_RE.search(text)
                if match and match.group(1) == expected_running:
                    return True, text
            except RuntimeError as exc:
                combined.append(str(exc))
            time.sleep(1.0)
        return False, "\n".join(combined)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", default=str(DEFAULT_IMAGE))
    parser.add_argument("--port", default="")
    parser.add_argument("--host", default="", help="laptop LAN IP visible to the device")
    parser.add_argument("--device-ip", default="192.168.86.72")
    parser.add_argument("--http-port", type=int, default=0)
    parser.add_argument("--out-dir", default="")
    parser.add_argument("--expected-variant", default="LunaSay")
    parser.add_argument("--dry-run", action="store_true",
                        help="inspect and hash the image without accessing device or network")
    args = parser.parse_args()

    image = Path(args.image).expanduser().resolve()
    if not image.is_file():
        raise SystemExit(f"error: firmware image not found: {image}")
    digest = sha256_hex(image)
    expected_identity = {
        **image_app_identity(image),
        "variant": args.expected_variant,
    }
    if args.dry_run:
        print(json.dumps({
            "dry_run": True,
            "image": str(image),
            "bytes": image.stat().st_size,
            "image_sha256": digest,
            "expected_identity": expected_identity,
        }, indent=2))
        return 0
    port = resolve_port(args.port)
    host = resolve_lan_ip(args.host)
    wrong_digest = ("0" if digest[0] != "0" else "1") + digest[1:]
    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    out_dir = Path(args.out_dir).expanduser() if args.out_dir else ROOT / "artifacts" / "qa" / f"lunasay-ota-recovery-{stamp}"
    out_dir.mkdir(parents=True, exist_ok=True)

    transcript: list[str] = []
    console = DeviceConsole(port, transcript)
    server, url = start_server(image, host, args.http_port)
    checks: dict[str, bool] = {}
    details: dict[str, str] = {}
    started = datetime.now().astimezone()

    try:
        console.connect()
        auto = console.command("ota auto off", ("ota: auto",), 5.0)
        checks["automatic_update_disabled_for_test"] = "ESP_OK" in auto and "interval_s=0" in auto

        console.command("ota boot factory", ("ota: boot factory",), 5.0)
        checks["factory_boot_before_test"], details["factory_boot_before_test"] = console.status("factory")
        wait_for_device_http(args.device_ip)
        checks["network_ready_before_remote_ota"] = True

        rejected = console.command(
            f"ota fetch {url} {wrong_digest}",
            ("install failed:", "sha256 mismatch"),
            90.0,
        )
        checks["wrong_sha_rejected"] = "sha256 mismatch" in rejected and "install failed:" in rejected
        status_text = console.command("ota status", ("ota: status",), 5.0)
        status_match = STATUS_RE.search(status_text)
        checks["remained_on_factory_after_rejection"] = bool(status_match and status_match.group(1) == "factory")

        accepted = console.command(
            f"ota fetch {url} {digest}",
            ("installed boot=", "install failed:"),
            120.0,
        )
        checks["correct_sha_installed"] = "installed boot=" in accepted and "install failed:" not in accepted
        product_ok = False
        for slot in ("ota_0", "ota_1"):
            product_ok, text = console.status(slot, timeout=70.0)
            if product_ok:
                details["product_boot_after_install"] = text
                details["product_slot"] = slot
                break
        checks["product_boot_after_install"] = product_ok
        if product_ok:
            wait_for_device_http(args.device_ip)
            installed_identity = battery_firmware_identity(args.device_ip)
            details["product_identity_after_install"] = json.dumps(installed_identity, sort_keys=True)
            checks["product_binary_identity_after_install"] = installed_identity == expected_identity
        else:
            checks["product_binary_identity_after_install"] = False

        console.command("ota boot factory", ("ota: boot factory",), 5.0)
        checks["commanded_factory_recovery"], details["commanded_factory_recovery"] = console.status("factory")

        console.command("ota boot ota", ("ota: boot ota",), 5.0)
        slot = details.get("product_slot", "ota_0")
        checks["commanded_product_recovery"], details["commanded_product_recovery"] = console.status(slot, timeout=90.0)
        if checks["commanded_product_recovery"]:
            wait_for_device_http(args.device_ip)
            recovered_identity = battery_firmware_identity(args.device_ip)
            details["product_identity_after_recovery"] = json.dumps(recovered_identity, sort_keys=True)
            checks["product_binary_identity_after_recovery"] = recovered_identity == expected_identity
        else:
            checks["product_binary_identity_after_recovery"] = False

        final_status = console.command("ota status", ("ota: status",), 5.0)
        details["final_status"] = final_status
        checks["no_crash_in_transcript"] = CRASH_RE.search("\n".join(transcript)) is None
    finally:
        console.close()
        server.shutdown()
        server.server_close()

    passed = all(checks.values())
    summary = {
        "schema": 1,
        "test": "LunaSay local OTA integrity and recovery",
        "passed": passed,
        "started_at": started.isoformat(),
        "finished_at": datetime.now().astimezone().isoformat(),
        "device_port": port,
        "firmware": {
            "path": str(image),
            "bytes": image.stat().st_size,
            "sha256": digest,
            "expected_identity": expected_identity,
        },
        "transport": {"url": url, "scope": "local-lab-http-with-explicit-sha256"},
        "checks": checks,
        "details": details,
        "production_signed_manifest_validated": False,
        "production_blockers": [
            "LunaSay release manifest is not yet published",
            "embedded OTA signing public key is labelled as a development key",
            "production signing private key is intentionally unavailable to this test",
        ],
    }
    (out_dir / "serial.log").write_text("\n".join(transcript) + "\n", encoding="utf-8")
    (out_dir / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    report = [
        "# LunaSay OTA and recovery validation",
        "",
        f"Result: **{'PASS' if passed else 'FAIL'}**",
        "",
        "This validates local image integrity rejection and factory/product recovery. It does not pass the production signed-manifest release gate.",
        "",
        "## Checks",
        "",
        *[f"- {'PASS' if value else 'FAIL'} — `{name}`" for name, value in checks.items()],
        "",
        "## Production blockers",
        "",
        *[f"- {item}" for item in summary["production_blockers"]],
        "",
    ]
    (out_dir / "report.md").write_text("\n".join(report), encoding="utf-8")
    print(json.dumps({"passed": passed, "out_dir": str(out_dir), "checks": checks}, indent=2))
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
