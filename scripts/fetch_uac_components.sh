#!/usr/bin/env bash
# Download ESP component registry archives for usb_device_uac (offline / CI).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="${ROOT}/uac/vendor_managed"
mkdir -p "$DEST"
python3 -m idf_component_manager registry sync "$DEST" \
  --component espressif/usb_device_uac==1.2.3 \
  --recursive \
  --resolution latest
echo "→ archives under ${DEST}/components/espressif/"
