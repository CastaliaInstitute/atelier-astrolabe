#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD_DIR="${ASTROLABE_WEB_SIM_BUILD_DIR:-${ROOT}/build/web-sim}"
PORT="${ASTROLABE_WEB_SIM_PORT:-8088}"

if [[ ! -f "${BUILD_DIR}/astrolabe-web-sim.html" ]]; then
  echo "error: missing ${BUILD_DIR}/astrolabe-web-sim.html; run ./tools/web-sim/build.sh first" >&2
  exit 1
fi

cd "${BUILD_DIR}"
python3 -m http.server "${PORT}"
