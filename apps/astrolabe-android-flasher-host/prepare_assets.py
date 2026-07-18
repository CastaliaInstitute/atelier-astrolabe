#!/usr/bin/env python3
"""Stage the current flasher and ESP-IDF build as Android APK assets."""

from __future__ import annotations

import importlib.util
from pathlib import Path
import shutil
import sys


root = Path(sys.argv[1]).resolve()
destination = Path(sys.argv[2]).resolve()
project = Path(__file__).resolve().parent

if destination.exists():
    shutil.rmtree(destination)
(destination / "www/flasher").mkdir(parents=True)
(destination / "www/vendor").mkdir(parents=True)
(destination / "www/bridge/artifacts").mkdir(parents=True)

for name in (
    "index.html",
    "flasher.js",
    "manifest.webmanifest",
    "service-worker.js",
    "icon-192.png",
    "icon-512.png",
):
    shutil.copy2(root / "docs/flasher" / name, destination / "www/flasher" / name)

shutil.copy2(root / "docs/styles.css", destination / "www/styles.css")
shutil.copy2(
    root / "docs/vendor/esptool-js-0.6.0.bundle.js",
    destination / "www/vendor/esptool-js-0.6.0.bundle.js",
)
shutil.copy2(
    project / "vendor/web-serial-polyfill.bundle.mjs",
    destination / "www/vendor/web-serial-polyfill.bundle.mjs",
)
shutil.copy2(
    project / "vendor/android-serial-bridge.mjs",
    destination / "www/vendor/android-serial-bridge.mjs",
)

flasher = destination / "www/flasher/flasher.js"
source = flasher.read_text(encoding="utf-8")
remote = 'from "https://esm.sh/web-serial-polyfill@1.0.15";'
local = 'from "../vendor/android-serial-bridge.mjs";'
if remote not in source:
    raise RuntimeError("The pinned WebSerial polyfill import was not found")
flasher.write_text(source.replace(remote, local), encoding="utf-8")

index = destination / "www/flasher/index.html"
index.write_text(
    index.read_text(encoding="utf-8").replace(
        "flasher.js?v=20260716a", "flasher.js?v=android-native-5"
    ),
    encoding="utf-8",
)
worker = destination / "www/flasher/service-worker.js"
worker.write_text(
    worker.read_text(encoding="utf-8")
    .replace("astrolabe-flasher-v1", "astrolabe-flasher-v5")
    .replace("flasher.js?v=20260716a", "flasher.js?v=android-native-5"),
    encoding="utf-8",
)

spec = importlib.util.spec_from_file_location(
    "android_flash_bridge", root / "scripts/android_flash_bridge.py"
)
if spec is None or spec.loader is None:
    raise RuntimeError("Could not load android_flash_bridge.py")
bridge = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bridge)
manifest, artifacts = bridge.make_manifest(
    root / "astrolabe175c/build",
    "faculty",
    display_name="Bundled local Astrolabe build",
)
(destination / "www/bridge/manifest.json").write_text(
    bridge.json.dumps(manifest, indent=2) + "\n", encoding="utf-8"
)
for name, path in artifacts.items():
    shutil.copy2(path, destination / "www/bridge/artifacts" / name)
