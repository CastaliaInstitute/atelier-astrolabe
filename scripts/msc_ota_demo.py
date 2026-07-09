#!/usr/bin/env python3
"""Stage a firmware image onto USB MSC and trigger local OTA over the CDC console."""

from __future__ import annotations

import argparse
import glob
import hashlib
import os
import platform
import subprocess
import shutil
import sys
import time
from pathlib import Path

try:
    import serial
    from serial import SerialException
except ImportError as exc:  # pragma: no cover - environment guard
    raise SystemExit("error: pyserial is required for msc_ota_demo.py") from exc


ROOT = Path(__file__).resolve().parent.parent
DEFAULT_TARGETS: dict[str, dict[str, object]] = {
    "astrolabe185b": {
        "image": ROOT / "astrolabe185b" / "build" / "astrolabe185b.bin",
        "relative": "update/astrolabe185b.bin",
        "preferred_ports": [
            "/dev/tty.usbmodemAstrolabe185B1",
            "/dev/cu.usbmodemAstrolabe185B1",
        ],
        "volume_env": "ASTROLABE185B_USBFLASH_VOLUME",
        "port_env": "ASTROLABE185B_OTA_PORT",
    },
    "astrolabe175c": {
        "image": ROOT / "astrolabe175c" / "build" / "astrolabe175c.bin",
        "relative": "update/astrolabe175c.bin",
        "preferred_ports": [
            "/dev/tty.usbmodemAstrolabe175C1",
            "/dev/cu.usbmodemAstrolabe175C1",
            "/dev/tty.usbmodem101",
            "/dev/cu.usbmodem101",
        ],
        "volume_env": "ASTROLABE175C_USBFLASH_VOLUME",
        "port_env": "ASTROLABE175C_OTA_PORT",
    },
}
DEFAULT_BAUD = 115200
MIN_FREE_HEADROOM = 64 * 1024


def sha256_hex(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def resolve_image(target: str, requested: str | None) -> Path:
    if requested:
        path = Path(requested).expanduser()
    else:
        path = Path(DEFAULT_TARGETS[target]["image"])  # type: ignore[arg-type]
    if not path.exists():
        raise SystemExit(f"error: image not found: {path}")
    return path


def resolve_volume(target: str, requested: str | None) -> Path:
    if requested:
        path = Path(requested).expanduser()
        if not path.is_dir():
            raise SystemExit(f"error: volume path not found: {path}")
        return path

    env_names = [
        str(DEFAULT_TARGETS[target]["volume_env"]),
        "ASTROLABE_USBFLASH_VOLUME",
    ]
    for env_name in env_names:
        value = os.environ.get(env_name)
        if value:
            path = Path(value).expanduser()
            if path.is_dir():
                return path

    candidates = [
        Path("/Volumes/USBFLASH"),
        Path("/Volumes/usbflash"),
        Path("/Volumes/Astrolabe185B"),
        Path("/Volumes/Astrolabe175C"),
    ]
    existing = [candidate for candidate in candidates if candidate.is_dir()]
    if len(existing) == 1:
        return existing[0]

    raise SystemExit(
        "error: could not auto-detect a mounted usbflash volume; pass --volume /Volumes/<name>"
    )


def resolve_port(target: str, requested: str | None) -> str:
    if requested:
        requested_path = requested
        if requested_path.startswith("/dev/cu.") and os.path.exists(requested_path.replace("/dev/cu.", "/dev/tty.", 1)):
            return requested_path.replace("/dev/cu.", "/dev/tty.", 1)
        return requested_path

    env_names = [
        str(DEFAULT_TARGETS[target]["port_env"]),
        "ASTROLABE_OTA_PORT",
        "ASTROLABE_UPLOAD_PORT",
        "ESPPORT",
        "IDF_PORT",
    ]
    for env_name in env_names:
        value = os.environ.get(env_name)
        if value:
            return value

    for candidate in DEFAULT_TARGETS[target]["preferred_ports"]:  # type: ignore[assignment]
        if os.path.exists(candidate):
            return str(candidate)

    ports = sorted(glob.glob("/dev/tty.usbmodem*")) + sorted(glob.glob("/dev/cu.usbmodem*"))
    if len(ports) == 1:
        return ports[0]
    if not ports:
        raise SystemExit("error: no usbmodem serial ports found")
    raise SystemExit("error: multiple serial ports found; pass --port explicitly")


def read_until_quiet(ser: serial.Serial, quiet_s: float = 0.35, max_s: float = 4.0) -> str:
    end = time.time() + max_s
    quiet_until = time.time() + quiet_s
    chunks: list[bytes] = []
    while time.time() < end:
        chunk = ser.read(4096)
        if chunk:
            chunks.append(chunk)
            quiet_until = time.time() + quiet_s
        elif time.time() >= quiet_until:
            break
    return b"".join(chunks).decode("utf-8", "replace")


def copy_image(image: Path, volume_root: Path, relative_path: str) -> Path:
    image_size = image.stat().st_size
    usage = shutil.disk_usage(volume_root)
    needed = image_size + MIN_FREE_HEADROOM
    if usage.total < image_size:
        raise SystemExit(
            f"error: mounted volume {volume_root} is too small for {image.name} "
            f"({usage.total} bytes total < {image_size} byte image)"
        )
    if usage.free < needed:
        raise SystemExit(
            f"error: mounted volume {volume_root} does not have enough free space for {image.name} "
            f"({usage.free} bytes free, need at least {needed} including headroom)"
        )
    if not os.access(volume_root, os.W_OK):
        raise SystemExit(f"error: mounted volume {volume_root} is not writable")
    destination = volume_root / relative_path
    try:
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(image, destination)
        with destination.open("rb") as copied:
            os.fsync(copied.fileno())
    except OSError as exc:
        raise SystemExit(f"error: failed to stage firmware onto {volume_root}: {exc.strerror or exc}") from exc
    return destination


def unmount_volume(volume_root: Path) -> None:
    if platform.system() != "Darwin":
        return
    try:
        subprocess.run(
            ["diskutil", "unmount", str(volume_root)],
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
        )
    except subprocess.CalledProcessError as exc:
        output = (exc.stdout or "").strip()
        raise SystemExit(f"error: failed to unmount staged volume {volume_root}: {output}") from exc


def issue_ota_command(port: str, baud: int, sha: str, command_timeout: float, settle_s: float) -> str:
    with serial.Serial(port, baud, timeout=0.2) as ser:
        ser.dtr = False
        ser.rts = False
        time.sleep(max(0.0, settle_s))
        ser.reset_input_buffer()
        ser.write(b"ota status\r\n")
        ser.flush()
        status = read_until_quiet(ser, quiet_s=0.25, max_s=1.5)

        command = f"ota usb {sha}\r\n"
        ser.write(command.encode("utf-8"))
        ser.flush()

        deadline = time.time() + command_timeout
        chunks: list[bytes] = []
        saw_accept = False
        saw_install = False
        while time.time() < deadline:
            try:
                chunk = ser.read(4096)
            except SerialException:
                if saw_install or saw_accept:
                    break
                raise
            if chunk:
                chunks.append(chunk)
                decoded = b"".join(chunks).decode("utf-8", "replace")
                if "ota: usb ESP_OK" in decoded:
                    saw_accept = True
                if "installed boot=" in decoded or "rebooting" in decoded:
                    saw_install = True
                if saw_install and ("rst:" in decoded or "ESP-ROM:" in decoded):
                    break
            elif saw_install:
                break
        output = status + b"".join(chunks).decode("utf-8", "replace")

    if "ota: usb ESP_OK" not in output:
        raise SystemExit(f"error: device did not accept ota usb\n{output}")
    if "installed boot=" not in output and "rebooting" not in output:
        raise SystemExit(f"error: OTA did not reach install/reboot confirmation\n{output}")
    return output


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("target", choices=sorted(DEFAULT_TARGETS))
    parser.add_argument("--image", help="firmware image to stage instead of the default build artifact")
    parser.add_argument("--volume", help="mounted usbflash volume root, e.g. /Volumes/USBFLASH")
    parser.add_argument("--port", help="CDC console port used to issue `ota usb`")
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD)
    parser.add_argument("--copy-only", action="store_true", help="copy the image but do not trigger OTA")
    parser.add_argument(
        "--sha256",
        help="expected SHA-256 to pass to `ota usb`; defaults to the staged image digest",
    )
    parser.add_argument("--settle", type=float, default=1.0, help="seconds to wait after opening serial")
    parser.add_argument("--timeout", type=float, default=120.0, help="seconds to wait for OTA logs")
    args = parser.parse_args()

    image = resolve_image(args.target, args.image)
    volume_root = resolve_volume(args.target, args.volume)
    relative_path = str(DEFAULT_TARGETS[args.target]["relative"])
    destination = copy_image(image, volume_root, relative_path)
    expected_sha = args.sha256 or sha256_hex(image)
    usage = shutil.disk_usage(volume_root)

    print(f"staged: target={args.target}")
    print(f"image:  {image}")
    print(f"dest:   {destination}")
    print(f"sha256: {expected_sha}")
    print(f"volume: {volume_root} total={usage.total} free={usage.free}")

    if args.copy_only:
        print("copy-only: firmware staged; run `ota usb <sha256>` on the device console to continue")
        return 0

    unmount_volume(volume_root)
    port = resolve_port(args.target, args.port)
    print(f"port:   {port}")
    output = issue_ota_command(port, args.baud, expected_sha, args.timeout, args.settle)
    sys.stdout.write(output)
    print(f"msc_ota_demo: OTA accepted and install logs observed for {args.target}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
