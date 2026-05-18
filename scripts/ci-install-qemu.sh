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

QEMU_TAG="${ESPRESSIF_QEMU_TAG:-esp-develop-9.2.2-20250817}"
QEMU_VER="${ESPRESSIF_QEMU_VER:-esp_develop_9.2.2_20250817}"

OS="$(uname -s)"
ARCH="$(uname -m)"
case "${OS}-${ARCH}" in
  Linux-x86_64) ASSET="qemu-xtensa-softmmu-${QEMU_VER}-x86_64-linux-gnu.tar.xz" ;;
  Linux-aarch64) ASSET="qemu-xtensa-softmmu-${QEMU_VER}-aarch64-linux-gnu.tar.xz" ;;
  Darwin-arm64) ASSET="qemu-xtensa-softmmu-${QEMU_VER}-aarch64-apple-darwin.tar.xz" ;;
  Darwin-x86_64) ASSET="qemu-xtensa-softmmu-${QEMU_VER}-x86_64-apple-darwin.tar.xz" ;;
  *)
    echo "error: unsupported host ${OS}-${ARCH} for bundled QEMU install" >&2
    exit 1
    ;;
esac

URL="https://github.com/espressif/qemu/releases/download/${QEMU_TAG}/${ASSET}"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
echo "→ download ${URL}"
curl -fsSL "$URL" -o "${TMP}/qemu.tar.xz"
mkdir -p "${QEMU_DIR}"
tar -xJf "${TMP}/qemu.tar.xz" -C "${QEMU_DIR}" --strip-components=1
chmod +x "$QEMU_BIN"
echo "→ installed ${QEMU_BIN}"
echo "${QEMU_BIN}"
