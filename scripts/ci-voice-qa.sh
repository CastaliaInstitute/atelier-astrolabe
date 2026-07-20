#!/usr/bin/env bash
# Self-hosted CI: optional flash -> all-face STT/TTS voice tour -> verified summary.
#
#   ./scripts/ci-voice-qa.sh
#   ASTROLABE_VOICE_LIMIT=3 ./scripts/ci-voice-qa.sh --no-flash
#
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

SKIP_FLASH=0
while [[ $# -gt 0 ]]; do
  case "$1" in
    --no-flash) SKIP_FLASH=1; shift ;;
    -h | --help)
      cat <<EOF
Usage: ci-voice-qa.sh [--no-flash]

  ./scripts/ci-flash.sh                 (unless --no-flash)
  ./scripts/stt_tts_face_tour.py        all ported faces
  ./scripts/stt_tts_face_tour.py --verify-summary <summary.json>

Env:
  ASTROLABE_VOICE_LIMIT       optional face count for shakedown runs
  ASTROLABE_VOICE_CAPTURE_MS  default 6500
EOF
      exit 0
      ;;
    *) echo "unknown option: $1" >&2; exit 1 ;;
  esac
done

./scripts/preflight-watch.sh
./scripts/ensure-device-secrets.sh

VENV="${ROOT}/mcp/astrolabe-esp/.venv"
if [[ ! -x "${VENV}/bin/python" ]]; then
  "${ROOT}/mcp/astrolabe-esp/setup.sh"
fi

if [[ "$SKIP_FLASH" != "1" ]]; then
  ./scripts/ci-flash.sh
else
  echo "→ skip flash (--no-flash)"
fi

PORT="$(./scripts/detect_upload_port.sh)"
export ASTROLABE_UPLOAD_PORT="$PORT"
echo "→ voice QA on ${PORT}"

"${VENV}/bin/python" "${ROOT}/scripts/stt_tts_face_tour.py" --self-test

VOICE_ARGS=(--port "$PORT" --capture-ms "${ASTROLABE_VOICE_CAPTURE_MS:-6500}")
VERIFY_ARGS=()
if [[ -n "${ASTROLABE_VOICE_LIMIT:-}" ]]; then
  VOICE_ARGS+=(--limit "${ASTROLABE_VOICE_LIMIT}")
  VERIFY_ARGS+=(--allow-limited)
fi

set +e
"${VENV}/bin/python" "${ROOT}/scripts/stt_tts_face_tour.py" "${VOICE_ARGS[@]}"
TOUR_RC=$?
set -e

SUMMARY="$(find artifacts/qa -maxdepth 2 -path '*/summary.json' -path '*stt-tts-face-tour-*' -print | sort | tail -1)"
if [[ -z "$SUMMARY" || ! -f "$SUMMARY" ]]; then
  echo "error: missing STT/TTS face tour summary" >&2
  if [[ "$TOUR_RC" == "0" ]]; then
    exit 2
  fi
  exit "$TOUR_RC"
fi

set +e
"${VENV}/bin/python" "${ROOT}/scripts/stt_tts_face_tour.py" --verify-summary "$SUMMARY" "${VERIFY_ARGS[@]}"
VERIFY_RC=$?
set -e
echo "→ voice summary ${SUMMARY}"
if [[ "$TOUR_RC" != "0" ]]; then
  exit "$TOUR_RC"
fi
exit "$VERIFY_RC"
