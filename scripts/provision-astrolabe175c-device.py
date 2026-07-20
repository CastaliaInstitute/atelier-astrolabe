#!/usr/bin/env python3
"""Provision a Faculty 1.75C device credential into Supabase."""

from __future__ import annotations

import argparse
import json
import os
import re
import urllib.request


MAC_RE = re.compile(r"^[0-9a-f]{2}(:[0-9a-f]{2}){5}$")
HEX64_RE = re.compile(r"^[0-9a-f]{64}$")


def normalize_mac(value: str) -> str:
    compact = value.lower().replace(":", "").replace("-", "")
    if len(compact) != 12 or not re.fullmatch(r"[0-9a-f]+", compact):
        raise SystemExit(f"invalid MAC: {value}")
    mac = ":".join(compact[i : i + 2] for i in range(0, 12, 2))
    if not MAC_RE.fullmatch(mac):
        raise SystemExit(f"invalid MAC: {value}")
    return mac


def short_id_from_mac(mac: str) -> str:
    """Match firmware's FNV-1a hash over NimBLE address byte order."""
    raw = bytes(int(part, 16) for part in mac.split(":"))
    h = 2166136261
    for b in reversed(raw):
        h ^= b
        h = (h * 16777619) & 0xFFFFFFFF
    short_id = ((h >> 16) ^ h) & 0xFFFF
    return f"{short_id:04x}"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mac", required=True, help="Device MAC from `device provision`.")
    parser.add_argument("--secret", required=True, help="64-char hex device secret from `device provision`.")
    parser.add_argument("--channel", default="astrolabe-faculty-amoled175")
    parser.add_argument("--label", default="")
    parser.add_argument(
        "--short-id",
        default="",
        help="Optional 4-hex display ID. Defaults to the firmware-compatible hash of --mac.",
    )
    parser.add_argument("--kind", default="astrolabe")
    parser.add_argument("--disabled", action="store_true")
    parser.add_argument("--url", default=os.environ.get("MYNAH_SUPABASE_URL") or os.environ.get("SUPABASE_URL"))
    parser.add_argument(
        "--service-role-key",
        default=os.environ.get("SUPABASE_SERVICE_ROLE_KEY") or os.environ.get("MYNAH_SUPABASE_SERVICE_ROLE_KEY"),
    )
    args = parser.parse_args()

    if not args.url or not args.service_role_key:
        raise SystemExit("set SUPABASE_URL/MYNAH_SUPABASE_URL and SUPABASE_SERVICE_ROLE_KEY")

    secret = args.secret.strip().lower()
    if not HEX64_RE.fullmatch(secret):
        raise SystemExit("secret must be 64 lowercase/uppercase hex characters")

    mac = normalize_mac(args.mac)
    short_id = args.short_id.strip().lower() or short_id_from_mac(mac)
    if not re.fullmatch(r"[0-9a-f]{4}", short_id):
        raise SystemExit("short ID must be four hex characters")

    row = {
        "mac": mac,
        "channel": args.channel.strip(),
        "device_secret": secret,
        "short_id": short_id,
        "kind": args.kind.strip() or "astrolabe",
        "enabled": not args.disabled,
    }
    if args.label:
        row["label"] = args.label

    endpoint = args.url.rstrip("/") + "/rest/v1/astrolabe_devices"
    req = urllib.request.Request(
        endpoint,
        data=json.dumps(row).encode("utf-8"),
        method="POST",
        headers={
            "Content-Type": "application/json",
            "apikey": args.service_role_key,
            "Authorization": f"Bearer {args.service_role_key}",
            "Prefer": "resolution=merge-duplicates,return=representation",
        },
    )
    with urllib.request.urlopen(req, timeout=20) as resp:
        print(resp.read().decode("utf-8"))


if __name__ == "__main__":
    main()
