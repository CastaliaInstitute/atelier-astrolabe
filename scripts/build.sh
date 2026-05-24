#!/usr/bin/env bash
# Reliable PlatformIO build when repo-local .pio/build fails (sync / full-disk races).
# Avoid running two `pio run` jobs at once — parallel builds can corrupt .d dependency dirs.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
export PLATFORMIO_BUILD_DIR="${PLATFORMIO_BUILD_DIR:-/tmp/astrolabe-pio-build}"
ENV="${PIO_ENV:-waveshare_s3_145}"
exec pio run -e "$ENV" -j 1 "$@"
