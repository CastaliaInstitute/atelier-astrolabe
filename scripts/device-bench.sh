#!/usr/bin/env bash
# m1 bench automation: pull integration → build → flash → comprehensive face/gesture test.
# Optionally run the all-face STT/TTS voice gate with ASTROLABE_BENCH_VOICE=1.
#
#   ./scripts/device-bench.sh
#   ./scripts/device-bench.sh --no-pull
#   ASTROLABE_BENCH_BRANCH=integration ./scripts/device-bench.sh
#
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

BRANCH="${ASTROLABE_BENCH_BRANCH:-integration}"
SKIP_PULL=0
SKIP_FLASH=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --no-pull) SKIP_PULL=1; shift ;;
    --no-flash) SKIP_FLASH=1; shift ;;
    -h | --help)
      cat <<EOF
Usage: device-bench.sh [--no-pull] [--no-flash]

  git pull origin ${BRANCH}
  ./scripts/ci-flash.sh          (unless --no-flash)
  ./scripts/functional_test.py   comprehensive matrix (L/R/U/D swipes + buttons)

Env:
  ASTROLABE_BENCH_BRANCH     default integration
  ASTROLABE_FT_MATRIX        default tests/functional/faces_astrolabe_comprehensive.json
  ASTROLABE_BENCH_VOICE      default 0; set 1 to run all-face STT/TTS tour
  ASTROLABE_VOICE_LIMIT      optional face count for voice shakedown
  ASTROLABE_USB_POWER_CYCLE  default 1
  ASTROLABE_UHUBCTL_SEARCH   default Espressif
EOF
      exit 0
      ;;
    *) echo "unknown option: $1" >&2; exit 1 ;;
  esac
done

export ASTROLABE_FT_MATRIX="${ASTROLABE_FT_MATRIX:-tests/functional/faces_astrolabe_comprehensive.json}"
export ASTROLABE_FT_SWIPE_NAV="${ASTROLABE_FT_SWIPE_NAV:-11}"
export ASTROLABE_USB_POWER_CYCLE="${ASTROLABE_USB_POWER_CYCLE:-1}"
export ASTROLABE_UHUBCTL_SEARCH="${ASTROLABE_UHUBCTL_SEARCH:-Espressif}"
export PATH="${HOME}/.astrolabe-ci-venv/bin:/opt/homebrew/bin:/usr/local/bin:${PATH}"

LOG_DIR="${ASTROLABE_BENCH_LOG_DIR:-${ROOT}/artifacts/bench}"
mkdir -p "$LOG_DIR"
STAMP="$(date -u '+%Y%m%d-%H%M%S')"
LOG="${LOG_DIR}/${STAMP}.log"
exec > >(tee -a "$LOG") 2>&1

echo "=== device-bench ${STAMP} ==="
echo "→ repo ${ROOT}"
echo "→ matrix ${ASTROLABE_FT_MATRIX}"

if [[ "$SKIP_PULL" != "1" ]]; then
  echo "→ git fetch origin ${BRANCH}"
  git fetch origin "${BRANCH}"
  git checkout "${BRANCH}"
  git pull --ff-only origin "${BRANCH}"
  echo "→ HEAD $(git rev-parse --short HEAD) $(git log -1 --format='%s')"
fi

./scripts/preflight-watch.sh
./scripts/ensure-device-secrets.sh

if [[ "$SKIP_FLASH" != "1" ]]; then
  echo "→ flash"
  ./scripts/ci-flash.sh
else
  echo "→ skip flash (--no-flash)"
fi

PORT="$(./scripts/detect_upload_port.sh)"
export ASTROLABE_UPLOAD_PORT="$PORT"
echo "→ functional test on ${PORT}"

VENV="${ROOT}/mcp/astrolabe-esp/.venv"
if [[ ! -x "${VENV}/bin/python" ]]; then
  "${ROOT}/mcp/astrolabe-esp/setup.sh"
fi

# ci-flash already uploaded; functional_test only re-flashes with --flash.
set +e
"${VENV}/bin/python" "${ROOT}/scripts/functional_test.py" --port "$PORT" --matrix "${ASTROLABE_FT_MATRIX}"
RC=$?
set -e

if [[ "$RC" == "0" && "${ASTROLABE_BENCH_VOICE:-0}" == "1" ]]; then
  echo "→ voice STT/TTS face tour on ${PORT}"
  "${VENV}/bin/python" "${ROOT}/scripts/stt_tts_face_tour.py" --self-test
  VOICE_ARGS=(--port "$PORT")
  if [[ -n "${ASTROLABE_VOICE_LIMIT:-}" ]]; then
    VOICE_ARGS+=(--limit "${ASTROLABE_VOICE_LIMIT}")
  fi
  set +e
  "${VENV}/bin/python" "${ROOT}/scripts/stt_tts_face_tour.py" "${VOICE_ARGS[@]}"
  RC=$?
  set -e
fi

ln -sf "${STAMP}.log" "${LOG_DIR}/latest.log"
if [[ -f artifacts/functional/latest/report.json ]]; then
  cp artifacts/functional/latest/report.json "${LOG_DIR}/${STAMP}-report.json"
  ln -sf "${STAMP}-report.json" "${LOG_DIR}/latest-report.json"
fi
VOICE_LATEST="$(find artifacts/qa -maxdepth 2 -path '*/summary.json' -path '*stt-tts-face-tour-*' -print 2>/dev/null | sort | tail -1 || true)"
if [[ -n "$VOICE_LATEST" && -f "$VOICE_LATEST" ]]; then
  if [[ "${ASTROLABE_BENCH_VOICE:-0}" == "1" && "$RC" == "0" ]]; then
    VERIFY_ARGS=(--verify-summary "$VOICE_LATEST")
    if [[ -n "${ASTROLABE_VOICE_LIMIT:-}" ]]; then
      VERIFY_ARGS+=(--allow-limited)
    fi
    set +e
    "${VENV}/bin/python" "${ROOT}/scripts/stt_tts_face_tour.py" "${VERIFY_ARGS[@]}"
    RC=$?
    set -e
  fi
  cp "$VOICE_LATEST" "${LOG_DIR}/${STAMP}-voice-summary.json"
  ln -sf "${STAMP}-voice-summary.json" "${LOG_DIR}/latest-voice-summary.json"
fi

echo "→ log ${LOG}"
echo "→ report artifacts/functional/latest/report.json"
if [[ -n "${VOICE_LATEST:-}" ]]; then
  echo "→ voice summary ${VOICE_LATEST}"
fi
exit "$RC"
