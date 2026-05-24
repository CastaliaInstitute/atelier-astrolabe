#!/usr/bin/env bash
# Flash one bench target, capture every face via USB serial `qa screen64`,
# and leave BMP/contact-sheet artifacts under artifacts/qa/serial-regression/.
#
#   ./scripts/ci-serial-screenshot-regression.sh s3_145
#   ./scripts/ci-serial-screenshot-regression.sh 1.75
#   ./scripts/ci-serial-screenshot-regression.sh 4c
#   ASTROLABE_REGRESSION_SKIP_UPLOAD=1 ASTROLABE_REGRESSION_ONLY=0,1 ./scripts/ci-serial-screenshot-regression.sh 1.45
#
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

TARGET="${1:-${ASTROLABE_REGRESSION_TARGET:-s3_145}}"
TARGET_LC="$(printf '%s' "$TARGET" | tr '[:upper:]' '[:lower:]')"

if [[ "$TARGET_LC" == "all" ]]; then
  for t in s3_145 s3_175 4c; do
    ASTROLABE_REGRESSION_TARGET="$t" "$0" "$t"
  done
  exit 0
fi

case "$TARGET_LC" in
  1.45|145|s3_145|waveshare_s3_145)
    LABEL="s3_145"
    ENV_NAME="waveshare_s3_145"
    DEFAULT_PORT="/dev/cu.usbmodem1101"
    ;;
  1.75|175|s3_175|waveshare_s3_175)
    LABEL="s3_175"
    ENV_NAME="waveshare_s3_175"
    DEFAULT_PORT="${ASTROLABE_S3_175_PORT:-}"
    ;;
  4c|p4|p4_4c|waveshare_4c|waveshare_p4_143)
    LABEL="4c"
    ENV_NAME="waveshare_4c"
    DEFAULT_PORT="/dev/cu.usbmodem1401"
    ;;
  *)
    echo "usage: $0 [1.45|s3_145|1.75|s3_175|4c|all]" >&2
    exit 64
    ;;
esac

export PLATFORMIO_BUILD_DIR="${PLATFORMIO_BUILD_DIR:-/tmp/astrolabe-pio-build}"
export PIO_ENV="$ENV_NAME"

PORT="${ASTROLABE_UPLOAD_PORT:-${ASTROLABE_SERIAL_PORT:-$DEFAULT_PORT}}"
if [[ -z "$PORT" ]]; then
  PORT="$(./scripts/detect_upload_port.sh)"
fi
export ASTROLABE_UPLOAD_PORT="$PORT"
export ASTROLABE_SERIAL_PORT="$PORT"

STAMP="$(date +%Y%m%d-%H%M%S)"
OUT_DIR="${ASTROLABE_REGRESSION_OUT_DIR:-${ROOT}/artifacts/qa/serial-regression/${LABEL}-${STAMP}}"
PAINT_SEC="${ASTROLABE_REGRESSION_PAINT_SEC:-1.4}"
TIMEOUT_SEC="${ASTROLABE_REGRESSION_TIMEOUT_SEC:-90}"
RETRIES="${ASTROLABE_REGRESSION_RETRIES:-1}"
SKIP_UPLOAD="${ASTROLABE_REGRESSION_SKIP_UPLOAD:-0}"
SKIP_UPLOADFS="${ASTROLABE_REGRESSION_SKIP_UPLOADFS:-0}"

log() { echo "→ $*"; }

log "target ${LABEL} env=${ENV_NAME} port=${PORT}"
log "artifacts ${OUT_DIR}"

if [[ "$SKIP_UPLOAD" != "1" ]]; then
  log "build + upload ${ENV_NAME}"
  ./scripts/build.sh -t upload --upload-port "$PORT"
fi

if [[ "$SKIP_UPLOADFS" != "1" && -d "${ROOT}/data" ]]; then
  log "upload LittleFS ${ENV_NAME}"
  pio run -e "$ENV_NAME" -t uploadfs --upload-port "$PORT"
fi

TOUR_ARGS=(
  --port "$PORT"
  --out-dir "$OUT_DIR"
  --paint-sec "$PAINT_SEC"
  --timeout-sec "$TIMEOUT_SEC"
  --retries "$RETRIES"
)
if [[ -n "${ASTROLABE_REGRESSION_START:-}" ]]; then
  TOUR_ARGS+=(--start "$ASTROLABE_REGRESSION_START")
fi
if [[ -n "${ASTROLABE_REGRESSION_END:-}" ]]; then
  TOUR_ARGS+=(--end "$ASTROLABE_REGRESSION_END")
fi
if [[ -n "${ASTROLABE_REGRESSION_ONLY:-}" ]]; then
  TOUR_ARGS+=(--only "$ASTROLABE_REGRESSION_ONLY")
fi

log "serial screenshot tour"
python3 "${ROOT}/scripts/serial_screenshot_tour.py" "${TOUR_ARGS[@]}"
log "contact sheet ${OUT_DIR}/contact-sheet.png"
