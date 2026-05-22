#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD_DIR="${ASTROLABE_WEB_SIM_BUILD_DIR:-${ROOT}/build/web-sim}"

if ! command -v emcmake >/dev/null 2>&1; then
  echo "error: emcmake not found. Activate Emscripten first, for example: source /path/to/emsdk_env.sh" >&2
  exit 1
fi

cmake_args=(
  -S "${ROOT}/tools/web-sim"
  -B "${BUILD_DIR}"
  -DCMAKE_BUILD_TYPE=Release
)

emcmake cmake "${cmake_args[@]}"
cmake --build "${BUILD_DIR}" --parallel

rm -rf "${BUILD_DIR}/assets"
cp -R "${ROOT}/tools/web-sim/public/assets" "${BUILD_DIR}/assets"

echo "${BUILD_DIR}/astrolabe-web-sim.html"
