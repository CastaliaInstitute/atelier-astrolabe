#!/usr/bin/env bash
# Install Espressif qemu-system-xtensa for Linux CI (ubuntu-latest).
set -euo pipefail

QEMU_DIR="${ESPRESSIF_QEMU_DIR:-${HOME}/.espressif/tools/qemu-xtensa}"
QEMU_BIN="${QEMU_DIR}/bin/qemu-system-xtensa"
if [[ -x "$QEMU_BIN" ]]; then
  echo "→ QEMU already installed: ${QEMU_BIN}"
  echo "${QEMU_BIN}"
  exit 0
fi

OS="$(uname -s)"
ARCH="$(uname -m)"
case "${OS}-${ARCH}" in
  Linux-x86_64) ASSET="qemu-xtensa-softmmu-esp_develop_2024_12_12-8e44c5b-v1.tar.xz" ;;
  Darwin-arm64) ASSET="qemu-xtensa-softmmu-esp_develop_2024_12_12-8e44c5b-v1-macos-arm64.tar.xz" ;;
  Darwin-x86_64) ASSET="qemu-xtensa-softmmu-esp_develop_2024_12_12-8e44c5b-v1-macos-x86_64.tar.xz" ;;
  *)
    echo "error: unsupported host ${OS}-${ARCH} for bundled QEMU install" >&2
    exit 1
    ;;
esac

URL="https://github.com/espressif/qemu/releases/download/esp-develop-2024-12-12-8e44c5b-v1/${ASSET}"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
echo "→ download ${URL}"
curl -fsSL "$URL" -o "${TMP}/qemu.tar.xz"
mkdir -p "${QEMU_DIR}"
tar -xJf "${TMP}/qemu.tar.xz" -C "${QEMU_DIR}" --strip-components=1
chmod +x "$QEMU_BIN"
echo "→ installed ${QEMU_BIN}"
echo "${QEMU_BIN}"
