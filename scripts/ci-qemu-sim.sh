#!/usr/bin/env bash
# Build QEMU profile firmware, merge flash image, run serial sim functional tests.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

ENV="${PIO_ENV:-waveshare_s3_175_qemu}"
BUILD_DIR="${PLATFORMIO_BUILD_DIR:-/tmp/astrolabe-pio-qemu}"
export PLATFORMIO_BUILD_DIR="$BUILD_DIR"
export PIO_ENV="$ENV"

echo "→ build ${ENV}"
./scripts/build.sh

BIN_DIR="${BUILD_DIR}/${ENV}"
for f in bootloader.bin partitions.bin firmware.bin; do
  if [[ ! -f "${BIN_DIR}/${f}" ]]; then
    echo "error: missing ${BIN_DIR}/${f}" >&2
    exit 1
  fi
done

FLASH_MERGED="${BUILD_DIR}/qemu_flash.bin"
echo "→ merge flash ${FLASH_MERGED}"
if command -v pio >/dev/null 2>&1; then
  pio pkg exec -p "tool-esptoolpy" -- esptool.py --chip esp32s3 merge_bin \
    --fill-flash-size 16MB \
    -o "$FLASH_MERGED" --flash_mode dio --flash_freq 80m --flash_size 16MB \
    0x0 "${BIN_DIR}/bootloader.bin" \
    0x8000 "${BIN_DIR}/partitions.bin" \
    0x10000 "${BIN_DIR}/firmware.bin"
else
  python3 -m esptool --chip esp32s3 merge_bin \
    --fill-flash-size 16MB \
    -o "$FLASH_MERGED" --flash_mode dio --flash_freq 80m --flash_size 16MB \
    0x0 "${BIN_DIR}/bootloader.bin" \
    0x8000 "${BIN_DIR}/partitions.bin" \
    0x10000 "${BIN_DIR}/firmware.bin"
fi

QEMU_BIN="$(bash ./scripts/ci-install-qemu.sh)"
export QEMU_ESP32="$QEMU_BIN"
exec python3 ./scripts/functional_sim_test.py --flash-bin "$FLASH_MERGED" --qemu "$QEMU_BIN"
