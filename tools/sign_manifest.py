#!/usr/bin/env python3
"""Sign face-pack manifest (Ed25519). Dev: writes stub signature when no key provided."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("manifest", type=Path)
    ap.add_argument("--sign-key", type=Path, default=None)
    ap.add_argument("--key-id", default="castalia-release-dev")
    args = ap.parse_args()

    data = json.loads(args.manifest.read_text())
    if args.sign_key and args.sign_key.exists():
        try:
            from cryptography.hazmat.primitives.serialization import load_pem_private_key
        except ImportError:
            print("pip install cryptography for Ed25519 signing", file=__import__("sys").stderr)
            return 1
        key = load_pem_private_key(args.sign_key.read_bytes(), password=None)
        # Phase 5: sign canonical manifest bytes
        data["signature"] = {"algorithm": "ed25519", "key_id": args.key_id, "value": "TODO"}
    else:
        data["signature"] = {"algorithm": "none", "key_id": args.key_id}
    args.manifest.write_text(json.dumps(data, indent=2) + "\n")
    print(f"updated {args.manifest}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
