#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
IMAGE="${ASTROLABE_WEB_SIM_EMSDK_IMAGE:-emscripten/emsdk:3.1.74}"

if command -v docker >/dev/null 2>&1 && docker info >/dev/null 2>&1; then
  runner=(docker run --rm -v "${ROOT}:/src" -w /src "${IMAGE}")
elif command -v podman >/dev/null 2>&1 && podman info >/dev/null 2>&1; then
  runner=(podman run --rm -v "${ROOT}:/src" -w /src "${IMAGE}")
else
  echo "error: Docker or Podman is required for container build, and no running engine was found" >&2
  exit 1
fi

"${runner[@]}" ./tools/web-sim/build.sh
