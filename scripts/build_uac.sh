#!/usr/bin/env bash
# Build waveshare_s3_175_uac: fetch IDF components, patch CMake, then full compile.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
export PLATFORMIO_BUILD_DIR="${PLATFORMIO_BUILD_DIR:-${HOME}/astrolabe-pio-build-uac}"
ENV=waveshare_s3_175_uac

echo "→ pio pkg install -e ${ENV}"
pio pkg install -e "${ENV}" >/dev/null

echo "→ clean stale local-component dependency"
python3 - <<'PY'
from pathlib import Path

for manifest in Path.home().glob(".platformio/packages/framework-arduinoespressif32*/idf_component.yml"):
    text = manifest.read_text(encoding="utf-8")
    lines = text.splitlines()
    out = []
    skip = False
    for line in lines:
        if line.startswith("  file://uac/overlay_usb_device_uac:") or line.startswith("  espressif/usb_device_uac:"):
            skip = True
            continue
        if skip:
            if line.startswith("    "):
                continue
            skip = False
        out.append(line)
    cleaned = "\n".join(out) + ("\n" if text.endswith("\n") else "")
    if cleaned != text:
        manifest.write_text(cleaned, encoding="utf-8")
PY

echo "→ remove legacy usb_device_uac overlay"
rm -rf "${ROOT}/managed_components/espressif__usb_device_uac"

echo "→ build ${ENV}"
PIO_ENV="${ENV}" "${ROOT}/scripts/build.sh" "$@"
