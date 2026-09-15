#!/usr/bin/env bash
# Populate an SD card (or any directory) with the Astrolabe Pythia payload:
#
#   <target>/pythia/index.html                       chat page (astrolabe185b/pythia_sd/)
#   <target>/pythia/vendor/wllama/index.js           llama.cpp WASM runtime (npm @wllama/wllama)
#   <target>/pythia/vendor/wllama/wasm/wllama.wasm
#   <target>/pythia/models/<model>.gguf              Qwen2.5-1.5B-Instruct by default (~1.1 GB)
#
# The watch (Pythia face) serves this tree at http://astrolabe.local/pythia/
# over USB NCM and Wi-Fi; the model runs in the browser that opens it.
#
# Usage:
#   scripts/pythia_sd_prepare.sh /Volumes/ASTROLABE
#   scripts/pythia_sd_prepare.sh /Volumes/ASTROLABE --model https://huggingface.co/.../file.gguf
#   scripts/pythia_sd_prepare.sh ./out --skip-model
#   scripts/pythia_sd_prepare.sh --push http://astrolabe.local     # no card reader: the
#                                  # firmware writes its own SD via PUT /pythia/* while the Pythia face is up
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WLLAMA_VERSION="${WLLAMA_VERSION:-3.6.1}"
MODEL_URL="${PYTHIA_MODEL_URL:-https://huggingface.co/Qwen/Qwen2.5-1.5B-Instruct-GGUF/resolve/main/qwen2.5-1.5b-instruct-q4_k_m.gguf}"
SKIP_MODEL=0
TARGET=""
PUSH_URL=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --model) MODEL_URL="$2"; shift 2 ;;
    --model=*) MODEL_URL="${1#--model=}"; shift ;;
    --skip-model) SKIP_MODEL=1; shift ;;
    --wllama) WLLAMA_VERSION="$2"; shift 2 ;;
    --push) PUSH_URL="${2%/}"; shift 2 ;;
    --push=*) PUSH_URL="${1#--push=}"; PUSH_URL="${PUSH_URL%/}"; shift ;;
    -h|--help) sed -n '2,17p' "$0"; exit 0 ;;
    *) if [[ -z "${TARGET}" ]]; then TARGET="$1"; shift; else echo "error: unexpected argument $1" >&2; exit 2; fi ;;
  esac
done

if [[ -n "${PUSH_URL}" ]]; then
  # Stage locally, then stream every file to the watch.
  TARGET="$(mktemp -d)"
  echo "== checking ${PUSH_URL}/api/pythia/config"
  if ! curl -fsS --max-time 10 "${PUSH_URL}/api/pythia/config" >/dev/null; then
    echo "error: ${PUSH_URL} is not answering /api/pythia/config (Pythia face selected? mDNS/NCM link?)" >&2
    exit 3
  fi
elif [[ -z "${TARGET}" ]]; then
  echo "error: target directory required (e.g. /Volumes/ASTROLABE) or --push <url>" >&2
  exit 2
elif [[ ! -d "${TARGET}" ]]; then
  echo "error: ${TARGET} is not a directory (is the SD card mounted?)" >&2
  exit 2
fi

PYTHIA_DIR="${TARGET}/pythia"
VENDOR_DIR="${PYTHIA_DIR}/vendor/wllama"
MODELS_DIR="${PYTHIA_DIR}/models"
CACHE_DIR="${PYTHIA_CACHE_DIR:-${HOME}/.cache/astrolabe-pythia}"
mkdir -p "${PYTHIA_DIR}" "${VENDOR_DIR}/wasm" "${MODELS_DIR}" "${CACHE_DIR}"

echo "== page"
cp "${ROOT}/astrolabe185b/pythia_sd/index.html" "${PYTHIA_DIR}/index.html"

echo "== wllama ${WLLAMA_VERSION}"
TARBALL="${CACHE_DIR}/wllama-${WLLAMA_VERSION}.tgz"
if [[ ! -s "${TARBALL}" ]]; then
  curl -fL --progress-bar -o "${TARBALL}" \
    "https://registry.npmjs.org/@wllama/wllama/-/wllama-${WLLAMA_VERSION}.tgz"
fi
EXTRACT_DIR="$(mktemp -d)"
tar -xzf "${TARBALL}" -C "${EXTRACT_DIR}" \
  package/esm/index.js package/esm/wasm/wllama.wasm package/LICENCE
cp "${EXTRACT_DIR}/package/esm/index.js" "${VENDOR_DIR}/index.js"
cp "${EXTRACT_DIR}/package/esm/wasm/wllama.wasm" "${VENDOR_DIR}/wasm/wllama.wasm"
cp "${EXTRACT_DIR}/package/LICENCE" "${VENDOR_DIR}/LICENSE"
echo "${WLLAMA_VERSION}" > "${VENDOR_DIR}/VERSION"
rm -rf "${EXTRACT_DIR}"

if [[ "${SKIP_MODEL}" -eq 0 ]]; then
  MODEL_FILE="$(basename "${MODEL_URL%%\?*}")"
  echo "== model ${MODEL_FILE}"
  CACHED="${CACHE_DIR}/${MODEL_FILE}"
  if [[ ! -s "${CACHED}" ]]; then
    # Resumable so a dropped connection does not restart a ~1 GB fetch.
    curl -fL --progress-bar -C - -o "${CACHED}" "${MODEL_URL}"
  fi
  if [[ ! -s "${MODELS_DIR}/${MODEL_FILE}" ]] || ! cmp -s "${CACHED}" "${MODELS_DIR}/${MODEL_FILE}"; then
    cp "${CACHED}" "${MODELS_DIR}/${MODEL_FILE}"
  fi
fi

if [[ -n "${PUSH_URL}" ]]; then
  echo "== pushing to ${PUSH_URL}/pythia/ (the watch writes /sdcard/pythia; ~1 MB/s over USB NCM, faster on Wi-Fi)"
  # Small files first so the page is usable while the GGUF streams.
  while IFS= read -r file; do
    rel="${file#${PYTHIA_DIR}/}"
    size="$(stat -f %z "${file}" 2>/dev/null || stat -c %s "${file}")"
    printf '%-60s %10s  ' "pythia/${rel}" "${size}"
    curl -fsS --max-time 7200 -T "${file}" "${PUSH_URL}/pythia/${rel}"
    echo
  done < <(find "${PYTHIA_DIR}" -type f -print0 | xargs -0 ls -S -r)
  rm -rf "${TARGET}"
  echo "== done: open ${PUSH_URL}/pythia/"
  exit 0
fi

echo "== done"
du -sh "${PYTHIA_DIR}"
find "${PYTHIA_DIR}" -type f | sed "s|^${TARGET}/||" | sort
cat <<EOF

Eject the card, insert it in the watch, and open http://astrolabe.local/pythia/
(USB NCM: the watch is also reachable at http://172.31.77.1/pythia/).
EOF
