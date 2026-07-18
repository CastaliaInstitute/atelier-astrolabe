#!/usr/bin/env python3
"""Serve a local Astrolabe build to the Android WebUSB flasher.

The server intentionally binds to loopback. ``adb reverse`` makes that loopback
port available as ``localhost`` on Android, which is a secure browser context
without exposing unpublished firmware on the LAN.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import mimetypes
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse


ROOT = Path(__file__).resolve().parents[1]
DOCS = ROOT / "docs"
DEFAULT_BUILD_DIR = ROOT / "astrolabe175c" / "build"
DEFAULT_PORT = 8097

FALLBACK_ARTIFACT_SPECS = (
    ("bootloader", "bootloader.bin", Path("bootloader/bootloader.bin"), "0x0"),
    ("partitions", "partition-table.bin", Path("partition_table/partition-table.bin"), "0x8000"),
    ("otadata", "ota_data_initial.bin", Path("ota_data_initial.bin"), "0x1a000"),
    ("app", "astrolabe175c.bin", Path("astrolabe175c.bin"), "0x20000"),
)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def git_value(*args: str, cwd: Path = ROOT, fallback: str) -> str:
    result = subprocess.run(
        ["git", *args],
        cwd=cwd,
        check=False,
        capture_output=True,
        text=True,
    )
    value = result.stdout.strip()
    return value or fallback


def artifact_specs(build_dir: Path) -> tuple[tuple[str, str, Path, str], ...]:
    args_path = build_dir / "flasher_args.json"
    if not args_path.is_file():
        return FALLBACK_ARTIFACT_SPECS
    data = json.loads(args_path.read_text(encoding="utf-8"))
    keys = (
        ("bootloader", "bootloader.bin", "bootloader"),
        ("partitions", "partition-table.bin", "partition-table"),
        ("otadata", "ota_data_initial.bin", "otadata"),
        ("app", "astrolabe175c.bin", "app"),
    )
    specs: list[tuple[str, str, Path, str]] = []
    for role, published_name, key in keys:
        entry = data.get(key)
        if not isinstance(entry, dict) or not entry.get("file") or not entry.get("offset"):
            raise ValueError(f"{args_path} is missing the {key} flash entry")
        specs.append((role, published_name, Path(entry["file"]), str(entry["offset"])))
    return tuple(specs)


def partition_app_slot_bytes(path: Path, app_address: int) -> int:
    data = path.read_bytes()
    for offset in range(0, len(data) - 31, 32):
        magic, part_type, _subtype, part_offset, size = struct.unpack_from("<HBBII", data, offset)
        if magic != 0x50AA:
            continue
        if part_type == 0x00 and part_offset == app_address:
            return size
    return 0x300000


def collect_artifacts(build_dir: Path) -> tuple[list[dict[str, object]], dict[str, Path]]:
    build_dir = build_dir.resolve()
    artifacts: list[dict[str, object]] = []
    paths: dict[str, Path] = {}
    missing: list[Path] = []
    for role, published_name, relative_path, address in artifact_specs(build_dir):
        path = (build_dir / relative_path).resolve()
        try:
            path.relative_to(build_dir)
        except ValueError as error:
            raise ValueError(f"flash artifact escapes build directory: {relative_path}") from error
        if not path.is_file():
            missing.append(path)
            continue
        paths[published_name] = path
        artifacts.append(
            {
                "name": published_name,
                "bytes": path.stat().st_size,
                "sha256": sha256(path),
                "address": address,
                "url": f"/bridge/artifacts/{published_name}",
                "role": role,
            }
        )
    if missing:
        details = "\n".join(f"  - {path}" for path in missing)
        raise FileNotFoundError(
            "The Android flash bridge needs a complete ESP-IDF build:\n"
            f"{details}\n"
            "Run with --build, or build with ./scripts/astrolabe175c_build.sh build first."
        )
    return artifacts, paths


def make_manifest(
    build_dir: Path,
    variant: str,
    *,
    source_root: Path = ROOT,
    source_ref: str | None = None,
    display_name: str | None = None,
) -> tuple[dict[str, object], dict[str, Path]]:
    artifacts, paths = collect_artifacts(build_dir)
    app = next(item for item in artifacts if item["role"] == "app")
    partitions = next(item for item in artifacts if item["role"] == "partitions")
    app_address = int(str(app["address"]), 16)
    app_slot_bytes = partition_app_slot_bytes(paths[str(partitions["name"])], app_address)
    git_sha = git_value("rev-parse", "--short=12", "HEAD", cwd=source_root, fallback="local")
    git_ref = source_ref or git_value("branch", "--show-current", cwd=source_root, fallback="working-tree")
    variant_label = variant.capitalize()
    channel = "astrolabe-cyber-175" if variant == "cyber" else "astrolabe-faculty-amoled175"
    release = {
        "release_id": f"local-{variant}-{git_sha}",
        "product_name": display_name or f"{git_ref} · {variant_label} ({git_sha[:7]})",
        "device_platform": "ESP32-S3 AMOLED 1.75C",
        "firmware_variant": variant_label,
        "ota_channel": channel,
        "git_ref": git_ref,
        "git_sha": git_sha,
        "firmware_bytes": app["bytes"],
        "app_slot_bytes": app_slot_bytes,
        "sha256": app["sha256"],
        "firmware_url": app["url"],
        "artifacts": artifacts,
        "source": "android-cli-bridge",
    }
    return {"git_ref": git_ref, "git_sha": git_sha, "releases": [release]}, paths


def run_build(variant: str, source_root: Path = ROOT, *, skip_lvgl_audit: bool = False) -> None:
    env = os.environ.copy()
    env["ASTROLABE175C_VARIANT"] = variant
    if skip_lvgl_audit:
        env["ASTROLABE175C_SKIP_LVGL_AUDIT"] = "1"
    subprocess.run(
        [str(source_root / "scripts" / "astrolabe175c_build.sh"), "build"],
        cwd=source_root,
        env=env,
        stdout=sys.stderr,
        check=True,
    )


def create_ref_worktree(ref: str, copy_local_secrets: bool) -> tuple[Path, Path, str]:
    resolved = subprocess.run(
        ["git", "rev-parse", "--verify", f"{ref}^{{commit}}"],
        cwd=ROOT,
        check=False,
        capture_output=True,
        text=True,
    )
    if resolved.returncode != 0:
        raise ValueError(
            f"unknown Git ref {ref!r}; fetch it first or use a local/remote-tracking ref such as origin/feature/name"
        )
    commit = resolved.stdout.strip()
    temp_root = Path(tempfile.mkdtemp(prefix="astrolabe-android-ref-"))
    worktree = temp_root / "source"
    try:
        subprocess.run(
            ["git", "worktree", "add", "--detach", str(worktree), commit],
            cwd=ROOT,
            check=True,
        )
        secrets = ROOT / "include" / "secrets.local.h"
        if copy_local_secrets and secrets.is_file():
            destination = worktree / "include" / "secrets.local.h"
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(secrets, destination)
    except Exception:
        if worktree.exists():
            subprocess.run(
                ["git", "worktree", "remove", "--force", str(worktree)],
                cwd=ROOT,
                check=False,
                capture_output=True,
            )
        shutil.rmtree(temp_root, ignore_errors=True)
        raise
    return worktree, temp_root, commit


def remove_ref_worktree(worktree: Path | None, temp_root: Path | None) -> None:
    if worktree is not None:
        subprocess.run(
            ["git", "worktree", "remove", "--force", str(worktree)],
            cwd=ROOT,
            check=False,
            capture_output=True,
        )
    if temp_root is not None:
        shutil.rmtree(temp_root, ignore_errors=True)


def adb_command(serial: str | None, *args: str, capture: bool = False) -> subprocess.CompletedProcess[str]:
    command = ["adb"]
    if serial:
        command.extend(["-s", serial])
    command.extend(args)
    return subprocess.run(command, check=False, capture_output=capture, text=True)


def choose_adb_device(requested: str | None) -> str | None:
    if not shutil.which("adb"):
        print("warning: adb not found; serve-only mode", file=sys.stderr)
        return None
    if requested:
        return requested
    result = adb_command(None, "devices", capture=True)
    devices = [
        line.split("\t", 1)[0]
        for line in result.stdout.splitlines()[1:]
        if line.endswith("\tdevice")
    ]
    if len(devices) == 1:
        return devices[0]
    if not devices:
        print(
            "warning: no ADB device found. Pair/connect Android with wireless debugging, then retry.",
            file=sys.stderr,
        )
    else:
        print("error: multiple ADB devices found; pass --adb-serial", file=sys.stderr)
    return None


def make_handler(manifest: dict[str, object], artifact_paths: dict[str, Path]):
    manifest_bytes = json.dumps(manifest, indent=2).encode("utf-8") + b"\n"

    class BridgeHandler(SimpleHTTPRequestHandler):
        def __init__(self, *args, **kwargs):
            super().__init__(*args, directory=str(DOCS), **kwargs)

        def end_headers(self) -> None:
            self.send_header("X-Content-Type-Options", "nosniff")
            self.send_header("Cross-Origin-Resource-Policy", "same-origin")
            super().end_headers()

        def send_bytes(self, body: bytes, content_type: str) -> None:
            self.send_response(200)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self) -> None:  # noqa: N802 - stdlib handler API
            path = urlparse(self.path).path
            if path == "/bridge/manifest.json":
                self.send_bytes(manifest_bytes, "application/json; charset=utf-8")
                return
            if path == "/bridge/status.json":
                self.send_bytes(b'{"ok":true,"bridge":"astrolabe-android"}\n', "application/json")
                return
            prefix = "/bridge/artifacts/"
            if path.startswith(prefix):
                name = path[len(prefix) :]
                artifact = artifact_paths.get(name)
                if artifact is None:
                    self.send_error(404, "Unknown build artifact")
                    return
                self.send_response(200)
                self.send_header("Content-Type", mimetypes.guess_type(name)[0] or "application/octet-stream")
                self.send_header("Content-Length", str(artifact.stat().st_size))
                self.send_header("Cache-Control", "no-store")
                self.end_headers()
                with artifact.open("rb") as stream:
                    shutil.copyfileobj(stream, self.wfile)
                return
            super().do_GET()

        def do_HEAD(self) -> None:  # noqa: N802 - stdlib handler API
            path = urlparse(self.path).path
            if path == "/bridge/manifest.json":
                self.send_response(200)
                self.send_header("Content-Type", "application/json; charset=utf-8")
                self.send_header("Content-Length", str(len(manifest_bytes)))
                self.send_header("Cache-Control", "no-store")
                self.end_headers()
                return
            if path == "/bridge/status.json":
                self.send_response(200)
                self.send_header("Content-Type", "application/json")
                self.send_header("Cache-Control", "no-store")
                self.end_headers()
                return
            prefix = "/bridge/artifacts/"
            if path.startswith(prefix):
                name = path[len(prefix) :]
                artifact = artifact_paths.get(name)
                if artifact is None:
                    self.send_error(404, "Unknown build artifact")
                    return
                self.send_response(200)
                self.send_header("Content-Type", mimetypes.guess_type(name)[0] or "application/octet-stream")
                self.send_header("Content-Length", str(artifact.stat().st_size))
                self.send_header("Cache-Control", "no-store")
                self.end_headers()
                return
            super().do_HEAD()

        def log_message(self, message: str, *args: object) -> None:
            print(f"bridge: {self.address_string()} {message % args}")

    return BridgeHandler


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", action="store_true", help="build firmware before starting the bridge")
    parser.add_argument("--build-dir", type=Path, default=DEFAULT_BUILD_DIR)
    parser.add_argument(
        "--ref",
        help="build a branch, tag, remote-tracking branch, or commit in an isolated temporary worktree",
    )
    parser.add_argument("--name", help="display name shown for this build in the PWA")
    parser.add_argument("--variant", choices=("faculty", "cyber"), default="faculty")
    parser.add_argument(
        "--no-local-secrets",
        action="store_true",
        help="do not copy gitignored include/secrets.local.h into a --ref worktree",
    )
    parser.add_argument(
        "--skip-lvgl-audit",
        action="store_true",
        help="set ASTROLABE175C_SKIP_LVGL_AUDIT=1 for legacy or experimental refs",
    )
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)
    parser.add_argument("--adb-serial", help="ADB device serial when more than one device is connected")
    parser.add_argument("--no-adb", action="store_true", help="do not install an ADB reverse tunnel")
    parser.add_argument("--no-open", action="store_true", help="do not open Chrome on Android")
    parser.add_argument("--check", action="store_true", help="validate and print the build manifest, then exit")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    worktree = None
    temp_root = None
    source_root = ROOT
    source_ref = None
    build_dir = args.build_dir
    try:
        if args.ref:
            if args.build_dir != DEFAULT_BUILD_DIR:
                raise ValueError("--ref and --build-dir cannot be used together")
            worktree, temp_root, _commit = create_ref_worktree(args.ref, not args.no_local_secrets)
            source_root = worktree
            source_ref = args.ref
            build_dir = worktree / "astrolabe175c" / "build"
            run_build(args.variant, worktree, skip_lvgl_audit=args.skip_lvgl_audit)
        elif args.build:
            run_build(args.variant, skip_lvgl_audit=args.skip_lvgl_audit)
        manifest, artifact_paths = make_manifest(
            build_dir,
            args.variant,
            source_root=source_root,
            source_ref=source_ref,
            display_name=args.name,
        )
    except (FileNotFoundError, ValueError, subprocess.CalledProcessError) as error:
        print(f"error: {error}", file=sys.stderr)
        remove_ref_worktree(worktree, temp_root)
        return 2
    except KeyboardInterrupt:
        print("\nBuild cancelled.", file=sys.stderr)
        remove_ref_worktree(worktree, temp_root)
        return 130
    if args.check:
        print(json.dumps(manifest, indent=2))
        remove_ref_worktree(worktree, temp_root)
        return 0

    url = f"http://localhost:{args.port}/flasher/?manifest=/bridge/manifest.json&bridge=1"
    adb_serial = None if args.no_adb else choose_adb_device(args.adb_serial)
    if adb_serial:
        reverse = adb_command(adb_serial, "reverse", f"tcp:{args.port}", f"tcp:{args.port}")
        if reverse.returncode != 0:
            print("error: adb reverse failed", file=sys.stderr)
            remove_ref_worktree(worktree, temp_root)
            return reverse.returncode

    try:
        server = ThreadingHTTPServer(("127.0.0.1", args.port), make_handler(manifest, artifact_paths))
    except OSError as error:
        print(f"error: could not start bridge: {error}", file=sys.stderr)
        if adb_serial:
            adb_command(adb_serial, "reverse", "--remove", f"tcp:{args.port}")
        remove_ref_worktree(worktree, temp_root)
        return 2
    print(f"Astrolabe Android flash bridge: {url}")
    print(f"Serving build: {build_dir.resolve()}")
    if source_ref:
        print(f"Source ref: {source_ref} @ {manifest['git_sha']}")
    if adb_serial:
        print(f"ADB device: {adb_serial} (reverse tcp:{args.port})")
        if not args.no_open:
            adb_command(
                adb_serial,
                "shell",
                "am",
                "start",
                "-a",
                "android.intent.action.VIEW",
                "-d",
                url,
            )
    else:
        print("Open the URL from a browser that can reach this loopback port.")
    print("Press Ctrl-C to stop.")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nStopping bridge.")
    finally:
        server.server_close()
        if adb_serial:
            adb_command(adb_serial, "reverse", "--remove", f"tcp:{args.port}")
        remove_ref_worktree(worktree, temp_root)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
