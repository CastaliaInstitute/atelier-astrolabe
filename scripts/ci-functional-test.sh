#!/usr/bin/env bash
# Self-hosted CI: flash → exercise every clock face (screenshot + qa inject) → report.json
#
#   ./scripts/ci-functional-test.sh
#   ASTROLABE_FT_FACES=classic,moon ./scripts/ci-functional-test.sh
#
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

./scripts/preflight-watch.sh
PORT="$(./scripts/detect_upload_port.sh)"
export ASTROLABE_UPLOAD_PORT="$PORT"
echo "→ port ${PORT}"
./scripts/ensure-device-secrets.sh

VENV="${ROOT}/mcp/astrolabe-esp/.venv"
if [[ ! -x "${VENV}/bin/python" ]]; then
  "${ROOT}/mcp/astrolabe-esp/setup.sh"
fi

FT_ARGS=(--flash --port "$PORT")
if [[ -n "${ASTROLABE_FT_FACES:-}" ]]; then
  FT_ARGS+=(--faces "$ASTROLABE_FT_FACES")
fi
if [[ -n "${ASTROLABE_FT_MATRIX:-}" ]]; then
  FT_ARGS+=(--matrix "$ASTROLABE_FT_MATRIX")
fi

"${VENV}/bin/python" "${ROOT}/scripts/functional_test.py" "${FT_ARGS[@]}"
