#!/usr/bin/env bash
# Install PlatformIO + MCP venv deps on self-hosted macOS (PEP 668) and Linux runners.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

VENV="${ASTROLABE_CI_VENV:-${HOME}/.astrolabe-ci-venv}"
if [[ ! -x "${VENV}/bin/pio" ]]; then
  python3 -m venv "$VENV"
  "${VENV}/bin/pip" install -U pip platformio pyserial
fi
export PATH="${VENV}/bin:${PATH}"

if [[ -n "${GITHUB_ENV:-}" ]]; then
  echo "PATH=${VENV}/bin:${PATH}" >>"$GITHUB_ENV"
fi

./mcp/astrolabe-esp/setup.sh
