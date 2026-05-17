#!/usr/bin/env bash
# Factory flash: firmware + optional face-pack / assets images (32 MB partition table).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
ENV="${PIO_ENV:-waveshare_s3_175}"
PORT="${ASTROLABE_PORT:-}"

echo "WARNING: custom partitions require full flash erase on first migration."
echo "  esptool.py --port <port> erase_flash"
echo "  then: $0"

./scripts/build.sh
pio run -e "$ENV" -t upload ${PORT:+-p "$PORT"}

FACE_IMG="${ROOT}/build/core-facepack.img"
if [[ -f "$FACE_IMG" ]]; then
  python3 tools/build_facepack.py --output "$FACE_IMG" 2>/dev/null || true
  if [[ -f "$FACE_IMG" ]]; then
    echo "Writing face pack to faces_a..."
  esptool.py write_flash 0x620000 "$FACE_IMG" 2>/dev/null || \
    echo "Skip face-pack write (adjust offset from partition table or use parttool.py)"
  fi
fi

echo "Done. Verify: esptool.py flash_id  (expect 32MB)"
