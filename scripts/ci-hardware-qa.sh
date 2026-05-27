#!/usr/bin/env bash
# Self-hosted CI: debug upload → JTAG face → WiFi screen.bmp → post to GitHub issue.
#
#   ASTROLABE_QA_FACE=moon ASTROLABE_QA_ISSUE=2 ./scripts/ci-hardware-qa.sh
#
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

FACE="${ASTROLABE_QA_FACE:-moon}"
ISSUE="${ASTROLABE_QA_ISSUE:-}"
MONITOR_SEC="${ASTROLABE_QA_MONITOR_SEC:-35}"
PAINT_SEC="${ASTROLABE_QA_PAINT_SEC:-3}"
# Release build is stable on device; set ASTROLABE_QA_DEBUG=1 for debug ELF + JTAG.
if [[ "${ASTROLABE_QA_DEBUG:-}" == "1" ]]; then
  DEBUG_ENV="${ASTROLABE_PIO_DEBUG_ENV:-waveshare_s3_175_debug}"
else
  DEBUG_ENV="${ASTROLABE_PIO_ENV:-waveshare_s3_175}"
fi
export PLATFORMIO_BUILD_DIR="${PLATFORMIO_BUILD_DIR:-/tmp/astrolabe-pio-build}"
QA_DIR="${ROOT}/artifacts/qa"
mkdir -p "$QA_DIR"

usage() {
  cat <<EOF
Usage: ci-hardware-qa.sh [--face NAME] [--issue N]

Environment:
  ASTROLABE_QA_FACE       Face name or index (default: moon)
  ASTROLABE_QA_ISSUE      GitHub issue number (required unless --issue)
  ASTROLABE_DEVICE_MAC    Stable target MAC; preferred over USB port names
  ASTROLABE_UPLOAD_PORT   USB port (auto-detect if unset)
  ASTROLABE_SECRETS_FILE  Path to secrets.local.h on runner host
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --face) FACE="$2"; shift 2 ;;
    --issue) ISSUE="$2"; shift 2 ;;
    --mac) export ASTROLABE_DEVICE_MAC="$2"; shift 2 ;;
    --mac=*) export ASTROLABE_DEVICE_MAC="${1#--mac=}"; shift ;;
    -h | --help) usage; exit 0 ;;
    *) echo "unknown option: $1" >&2; usage >&2; exit 1 ;;
  esac
done

if [[ -z "$ISSUE" ]]; then
  echo "error: set ASTROLABE_QA_ISSUE or --issue N" >&2
  exit 1
fi

ensure_secrets() {
  if [[ -f "${ROOT}/include/secrets.local.h" ]]; then
    return 0
  fi
  local src="${ASTROLABE_SECRETS_FILE:-${HOME}/GitHub/CastaliaInstitute/astrolabe/include/secrets.local.h}"
  if [[ -f "$src" ]]; then
    cp "$src" "${ROOT}/include/secrets.local.h"
    echo "→ secrets from ${src}"
    return 0
  fi
  if [[ -n "${MYNAH_WIFI_SSID:-}" ]]; then
    "${ROOT}/scripts/write_secrets_ci.sh"
    return 0
  fi
  echo "error: no secrets.local.h — add file or GitHub Actions secrets" >&2
  exit 1
}

PORT="$(./scripts/detect_upload_port.sh)"
export ASTROLABE_UPLOAD_PORT="$PORT"
echo "→ port ${PORT}"
[[ -n "${ASTROLABE_DEVICE_MAC:-}" ]] && echo "→ mac ${ASTROLABE_DEVICE_MAC}"

ensure_secrets

echo "→ build + upload ${DEBUG_ENV}"
export PIO_ENV="$DEBUG_ENV"
./scripts/build.sh -t upload --upload-port "$PORT"

# MCP venv only needed for face name → index in jtag_set_face.sh
if [[ ! -x "${ROOT}/mcp/astrolabe-esp/.venv/bin/python" ]]; then
  "${ROOT}/mcp/astrolabe-esp/setup.sh"
fi

set_face_serial() {
  echo "→ serial: face ${FACE}"
  "${ROOT}/mcp/astrolabe-esp/.venv/bin/python" -u - "$PORT" "$FACE" <<'PY'
import sys, time, serial
port, face = sys.argv[1], sys.argv[2]
ser = serial.Serial(port, 115200, timeout=0.3)
time.sleep(1.0)
ser.reset_input_buffer()
ser.write(f"face {face}\n".encode())
ser.flush()
time.sleep(0.5)
for _ in range(40):
    chunk = ser.read(4096)
    if chunk:
        print(chunk.decode("utf-8", errors="replace"), end="")
    time.sleep(0.1)
ser.close()
PY
}

if [[ "${ASTROLABE_QA_DEBUG:-}" == "1" ]]; then
  echo "→ JTAG set face: ${FACE}"
  if ! "${ROOT}/scripts/jtag_set_face.sh" "$FACE" 2>/dev/null; then
    echo "→ JTAG failed; using serial face command"
    set_face_serial
  fi
else
  echo "→ serial set face: ${FACE} (release build)"
  sleep 5
  set_face_serial
fi

LOG="${QA_DIR}/serial-$(date +%s).log"
export ASTROLABE_MONITOR_LOG="$LOG"
export ASTROLABE_SERIAL_PORT="$PORT"

echo "→ serial capture ${MONITOR_SEC}s (WiFi + screen server)"
"${ROOT}/scripts/monitor_capture.sh" "$MONITOR_SEC" || true

IP="$(grep -Eo 'Screen over WiFi: http://[0-9.]+' "$LOG" | head -1 | grep -Eo '[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+' || true)"
if [[ -z "$IP" ]]; then
  echo "error: watch did not print WiFi screen URL — check secrets and WiFi in ${LOG}" >&2
  tail -40 "$LOG" >&2 || true
  exit 1
fi
echo "→ watch IP ${IP}"

sleep "$PAINT_SEC"

STAMP="$(date +%Y%m%d-%H%M%S)"
BMP="${QA_DIR}/${FACE}-${STAMP}.bmp"
PNG="${QA_DIR}/${FACE}-${STAMP}.png"

echo "→ GET http://${IP}/screen.bmp"
curl -sfS "http://${IP}/screen.bmp" -o "$BMP"
if [[ ! -s "$BMP" ]]; then
  echo "error: empty screen.bmp" >&2
  exit 1
fi

if command -v sips >/dev/null 2>&1; then
  sips -s format png "$BMP" --out "$PNG" >/dev/null
elif command -v magick >/dev/null 2>&1; then
  magick "$BMP" "$PNG"
else
  "${ROOT}/mcp/astrolabe-esp/.venv/bin/python" - <<PY
from pathlib import Path
try:
    from PIL import Image
except ImportError:
    import subprocess, sys
    subprocess.check_call([sys.executable, "-m", "pip", "install", "-q", "pillow"])
    from PIL import Image
bmp, png = Path("${BMP}"), Path("${PNG}")
Image.open(bmp).save(png)
PY
fi

echo "→ post screenshot to issue #${ISSUE}"
"${ROOT}/scripts/post_issue_screenshot.sh" "$ISSUE" "$PNG" "QA ${FACE} @ ${STAMP}"

echo "✓ hardware QA complete: ${PNG}"
