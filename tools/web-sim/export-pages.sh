#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD_DIR="${ASTROLABE_WEB_SIM_BUILD_DIR:-${ROOT}/build/web-sim}"
OUT_DIR="${ASTROLABE_WEB_SIM_PAGES_DIR:-${ROOT}/docs/sim}"

"${ROOT}/tools/web-sim/build.sh"

rm -rf "${OUT_DIR}"
mkdir -p "${OUT_DIR}"
cp "${BUILD_DIR}/astrolabe-web-sim.html" "${OUT_DIR}/index.html"
cp "${BUILD_DIR}/astrolabe-web-sim.js" "${OUT_DIR}/"
cp "${BUILD_DIR}/astrolabe-web-sim.wasm" "${OUT_DIR}/"
cp -R "${BUILD_DIR}/assets" "${OUT_DIR}/assets"

echo "${OUT_DIR}/index.html"
