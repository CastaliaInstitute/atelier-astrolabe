#!/usr/bin/env python3
"""Create a signed Astrolabe 1.75C OTA manifest."""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
import re
import subprocess
import tempfile
from pathlib import Path


DEFAULT_CHANNEL = "astrolabe-faculty-amoled175"
SIG_ALG = "ecdsa-p256-sha256"
MAC_RE = re.compile(r"^[0-9a-f]{2}(:[0-9a-f]{2}){5}$")


def normalize_mac(mac: str) -> str:
    compact = "".join(ch.lower() for ch in mac if ch not in ":- ")
    if len(compact) != 12 or any(ch not in "0123456789abcdef" for ch in compact):
        raise ValueError(f"invalid MAC address: {mac}")
    normalized = ":".join(compact[i : i + 2] for i in range(0, 12, 2))
    if not MAC_RE.match(normalized):
        raise ValueError(f"invalid MAC address: {mac}")
    return normalized


def load_devices(values: list[str], devices_file: Path | None) -> list[str]:
    devices = list(values)
    if devices_file is not None:
        for line in devices_file.read_text(encoding="utf-8").splitlines():
            line = line.split("#", 1)[0].strip()
            if line:
                devices.append(line)
    normalized = sorted(set(normalize_mac(device) for device in devices))
    if not normalized:
        raise SystemExit("at least one --device-mac or --devices-file entry is required")
    return normalized


def canonical(channel: str, firmware_variant: str, firmware_url: str, sha256: str, size: int, devices: list[str]) -> bytes:
    return (
        f"ota_channel={channel}\n"
        f"firmware_variant={firmware_variant}\n"
        f"firmware_url={firmware_url}\n"
        f"sha256={sha256}\n"
        f"bytes={size}\n"
        f"devices={','.join(devices)}\n"
    ).encode("ascii")


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def sign_payload(private_key: Path, payload: bytes) -> str:
    with tempfile.TemporaryDirectory() as td:
        payload_path = Path(td) / "payload.txt"
        sig_path = Path(td) / "payload.sig"
        payload_path.write_bytes(payload)
        subprocess.run(
            ["openssl", "dgst", "-sha256", "-sign", str(private_key), "-out", str(sig_path), str(payload_path)],
            check=True,
        )
        return base64.b64encode(sig_path.read_bytes()).decode("ascii")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", type=Path, required=True, help="Firmware .bin to describe.")
    parser.add_argument("--channel", default=DEFAULT_CHANNEL, help="OTA channel embedded in and signed by the manifest.")
    parser.add_argument("--firmware-variant", default="Faculty", help="Build variant embedded in and signed by the manifest.")
    parser.add_argument("--firmware-url", required=True, help="URL devices will fetch for the firmware .bin.")
    parser.add_argument("--private-key", type=Path, required=True, help="ECDSA P-256 PEM private key.")
    parser.add_argument("--out", type=Path, required=True, help="Manifest JSON output path.")
    parser.add_argument("--device-mac", action="append", default=[], help="Provisioned device MAC allowed to install.")
    parser.add_argument("--devices-file", type=Path, help="Text file of provisioned MAC addresses, one per line.")
    args = parser.parse_args()

    image = args.image
    size = image.stat().st_size
    digest = sha256_file(image)
    devices = load_devices(args.device_mac, args.devices_file)
    payload = canonical(args.channel, args.firmware_variant, args.firmware_url, digest, size, devices)
    manifest = {
        "ota_channel": args.channel,
        "firmware_variant": args.firmware_variant,
        "firmware_url": args.firmware_url,
        "sha256": digest,
        "bytes": size,
        "devices": devices,
        "sig_alg": SIG_ALG,
        "signature": sign_payload(args.private_key, payload),
    }
    args.out.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(args.out)


if __name__ == "__main__":
    main()
