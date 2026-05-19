#!/usr/bin/env bash
# Step through each clock face: boot gate → WiFi → serial face → screen.bmp checks.
#
#   ./scripts/ci-face-regression.sh
#   ASTROLABE_REGRESSION_SKIP_UPLOAD=1 ./scripts/ci-face-regression.sh
#   ASTROLABE_REGRESSION_FACES=moon,calcifer,cycle ./scripts/ci-face-regression.sh
#
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

ENV="${ASTROLABE_PIO_ENV:-waveshare_s3_175}"
export PLATFORMIO_BUILD_DIR="${PLATFORMIO_BUILD_DIR:-/tmp/astrolabe-pio-build}"
QA_DIR="${ROOT}/artifacts/qa/regression"
BOOT_SEC="${ASTROLABE_REGRESSION_BOOT_SEC:-22}"
WIFI_SEC="${ASTROLABE_REGRESSION_WIFI_SEC:-40}"
PAINT_SEC="${ASTROLABE_REGRESSION_PAINT_SEC:-4}"
SKIP_UPLOAD="${ASTROLABE_REGRESSION_SKIP_UPLOAD:-0}"
DEFAULT_FACES="classic,apocalypso,digital,spotify,astro,moon,calcifer,cycle,castalia"
FACES="${ASTROLABE_REGRESSION_FACES:-$DEFAULT_FACES}"

mkdir -p "$QA_DIR"
STAMP="$(date +%Y%m%d-%H%M%S)"
SUMMARY="${QA_DIR}/summary-${STAMP}.md"
FAIL=0
PASSED=()
FAILED=()

log() { echo "→ $*"; }

ensure_secrets() {
  if [[ -f "${ROOT}/include/secrets.local.h" ]]; then
    return 0
  fi
  local src="${ASTROLABE_SECRETS_FILE:-${HOME}/GitHub/CastaliaInstitute/astrolabe/include/secrets.local.h}"
  if [[ -f "$src" ]]; then
    cp "$src" "${ROOT}/include/secrets.local.h"
    log "secrets from ${src}"
    return 0
  fi
  if [[ -n "${MYNAH_WIFI_SSID:-}" ]]; then
    "${ROOT}/scripts/write_secrets_ci.sh"
    return 0
  fi
  echo "error: no secrets.local.h — WiFi required for screen.bmp" >&2
  exit 1
}

boot_gate() {
  local log="$1"
  if [[ ! -s "$log" ]]; then
    echo "boot gate: empty serial log" >&2
    return 1
  fi
  if grep -qE 'Guru Meditation|abort\(\)|Brownout|panic|Stack canary' "$log"; then
    echo "boot gate: panic/crash in serial log" >&2
    return 1
  fi
  local rom_count
  rom_count="$(grep -c 'ESP-ROM:esp32s3' "$log" 2>/dev/null || true)"
  if [[ "$rom_count" -gt 3 ]] && ! grep -qE 'Astrolabe ready|MVP ready|Screen over WiFi' "$log"; then
    echo "boot gate: ESP-ROM boot loop (${rom_count} lines)" >&2
    return 1
  fi
  if ! grep -qE 'Astrolabe ready|MVP ready|Screen over WiFi|Settings: http://' "$log"; then
    echo "boot gate: no ready banner or screen URL" >&2
    return 1
  fi
  return 0
}

discover_ip() {
  if [[ -n "${ASTROLABE_WATCH_IP:-}" ]]; then
    echo "$ASTROLABE_WATCH_IP"
    return 0
  fi
  local log="$1"
  local ip
  ip="$(grep -Eo 'Screen over WiFi: http://[0-9.]+' "$log" 2>/dev/null | head -1 | grep -Eo '[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+' || true)"
  [[ -n "$ip" ]] && echo "$ip" && return 0
  ip="$(grep -Eo 'Settings: http://[0-9.]+' "$log" 2>/dev/null | head -1 | grep -Eo '[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+' || true)"
  [[ -n "$ip" ]] && echo "$ip" && return 0
  return 1
}

check_bmp() {
  local bmp="$1"
  local face="$2"
  if [[ ! -s "$bmp" ]]; then
    echo "empty bmp"
    return 1
  fi
  local sz
  sz="$(wc -c <"$bmp" | tr -d ' ')"
  if [[ "$sz" -lt 50000 ]]; then
    echo "bmp too small (${sz} bytes)"
    return 1
  fi
  if [[ ! -x "${ROOT}/mcp/astrolabe-esp/.venv/bin/python" ]]; then
    "${ROOT}/mcp/astrolabe-esp/setup.sh"
  fi
  "${ROOT}/mcp/astrolabe-esp/.venv/bin/python" - "$bmp" <<'PY'
import struct, sys
path = sys.argv[1]
with open(path, "rb") as f:
    hdr = f.read(54)
if len(hdr) < 54 or hdr[0:2] != b"BM":
    print("not a BMP")
    sys.exit(1)
# BITMAPINFOHEADER offset 18: width @26, height @30 (little-endian)
w = struct.unpack_from("<i", hdr, 18)[0]
h = struct.unpack_from("<i", hdr, 22)[0]
if w <= 0 or h <= 0:
    w = struct.unpack_from("<i", hdr, 26)[0]
    h = abs(struct.unpack_from("<i", hdr, 30)[0])
row = ((w * 3 + 3) // 4) * 4
need = 54 + row * h
with open(path, "rb") as f:
    f.seek(54)
    samples = []
    step_y = max(1, h // 16)
    step_x = max(1, w // 16)
    for y in range(0, h, step_y):
        f.seek(54 + (h - 1 - y) * row)
        rowb = f.read(row)
        for x in range(0, min(w, len(rowb) // 3), step_x):
            i = x * 3
            b, g, r = rowb[i], rowb[i + 1], rowb[i + 2]
            samples.append(r + g + b)
    if not samples:
        print("no pixels sampled")
        sys.exit(1)
    mean = sum(samples) / len(samples)
    dark = sum(1 for v in samples if v < 12)
    if mean < 8.0 or dark > len(samples) * 0.98:
        print(f"display too dark (mean={mean:.1f})")
        sys.exit(1)
print(f"ok {w}x{h} mean={mean:.1f}")
PY
}

PORT="$(./scripts/detect_upload_port.sh)"
export ASTROLABE_UPLOAD_PORT="$PORT"
export ASTROLABE_SERIAL_PORT="$PORT"
log "port ${PORT}"

ensure_secrets

if [[ "$SKIP_UPLOAD" != "1" ]]; then
  log "build + upload ${ENV}"
  PIO_ENV="$ENV" ./scripts/build.sh -t upload --upload-port "$PORT"
else
  log "skip upload (ASTROLABE_REGRESSION_SKIP_UPLOAD=1)"
fi

BOOT_LOG="${QA_DIR}/boot-serial-${STAMP}.log"
export ASTROLABE_MONITOR_LOG="$BOOT_LOG"
unset ASTROLABE_MONITOR_NO_RESET
log "boot gate serial (${BOOT_SEC}s, DTR reset)"
"${ROOT}/scripts/monitor_capture.sh" "$BOOT_SEC" || true

if ! boot_gate "$BOOT_LOG"; then
  echo "✗ boot gate failed — see ${BOOT_LOG}" >&2
  tail -30 "$BOOT_LOG" >&2 || true
  {
    echo "# Face regression ${STAMP}"
    echo ""
    echo "**Result:** FAILED (boot gate)"
    echo ""
    echo "Log: \`${BOOT_LOG}\`"
  } >"$SUMMARY"
  exit 1
fi

IP="$(discover_ip "$BOOT_LOG" || true)"
if [[ -z "$IP" ]]; then
  WIFI_LOG="${QA_DIR}/wifi-serial-${STAMP}.log"
  export ASTROLABE_MONITOR_LOG="$WIFI_LOG"
  log "WiFi discovery serial (${WIFI_SEC}s)"
  "${ROOT}/scripts/monitor_capture.sh" "$WIFI_SEC" || true
  IP="$(discover_ip "$WIFI_LOG" || discover_ip "$BOOT_LOG" || true)"
fi

if [[ -z "$IP" ]]; then
  echo "✗ no watch IP (set ASTROLABE_WATCH_IP or fix WiFi)" >&2
  {
    echo "# Face regression ${STAMP}"
    echo ""
    echo "**Result:** FAILED (no WiFi IP)"
    echo ""
    echo "Boot log: \`${BOOT_LOG}\`"
  } >"$SUMMARY"
  exit 1
fi
log "watch IP ${IP}"

IFS=',' read -r -a FACE_ARR <<<"$FACES"
for face in "${FACE_ARR[@]}"; do
  face="$(echo "$face" | tr -d ' ')"
  [[ -z "$face" ]] && continue
  log "face ${face}"
  face_ok=1
  face_msg=""
  if ! "${ROOT}/scripts/serial_set_face.sh" "$PORT" "$face" >/dev/null; then
    face_ok=0
    face_msg="serial face command failed"
  else
    sleep "$PAINT_SEC"
    BMP="${QA_DIR}/${face}-${STAMP}.bmp"
    if ! curl -sfS "http://${IP}/screen.bmp" -o "$BMP"; then
      face_ok=0
      face_msg="screen.bmp fetch failed"
    elif ! out="$(check_bmp "$BMP" "$face" 2>&1)"; then
      face_ok=0
      face_msg="$out"
    else
      face_msg="$out"
    fi
  fi
  if [[ "$face_ok" == "1" ]]; then
    PASSED+=("$face")
    echo "  ✓ ${face}: ${face_msg}"
  else
    FAILED+=("$face")
    FAIL=1
    echo "  ✗ ${face}: ${face_msg}" >&2
  fi
done

LOG_TXT="${QA_DIR}/device-logs-${STAMP}.txt"
if curl -sfS "http://${IP}/logs.txt" -o "$LOG_TXT"; then
  echo "→ device logs: http://${IP}/logs (${LOG_TXT})"
else
  LOG_TXT=""
fi

{
  echo "# Face regression ${STAMP}"
  echo ""
  echo "- **IP:** ${IP}"
  echo "- **Log browser:** http://${IP}/logs"
  if [[ -n "$LOG_TXT" ]]; then
    echo "- **Logs file:** \`${LOG_TXT}\`"
  fi
  echo "- **Port:** ${PORT}"
  echo "- **Faces:** ${FACES}"
  echo ""
  if [[ "$FAIL" == "0" ]]; then
    echo "**Result:** PASSED (${#PASSED[@]}/${#FACE_ARR[@]})"
  else
    echo "**Result:** FAILED (${#PASSED[@]} passed, ${#FAILED[@]} failed)"
  fi
  echo ""
  if [[ ${#PASSED[@]} -gt 0 ]]; then
    echo "## Passed"
    for f in "${PASSED[@]}"; do echo "- ${f}"; done
    echo ""
  fi
  if [[ ${#FAILED[@]} -gt 0 ]]; then
    echo "## Failed"
    for f in "${FAILED[@]}"; do echo "- ${f}"; done
    echo ""
  fi
  echo "Artifacts: \`${QA_DIR}/\`"
} >"$SUMMARY"

cat "$SUMMARY"
if [[ "$FAIL" != "0" ]]; then
  exit 1
fi
echo "✓ face regression complete: ${SUMMARY}"
