#!/usr/bin/env bash
# Build Astrolabe locally, bundle that exact build into the Android flasher, and optionally install it.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
APP_DIR="${ROOT}/apps/astrolabe-android-flasher-host"
VARIANT="${ASTROLABE175C_VARIANT:-faculty}"
BUILD_FIRMWARE=1
INSTALL=0
SKIP_LVGL_AUDIT=0
ADB_SERIAL="${ADB_SERIAL:-}"
FAILED_BUILD_MARKER="${ROOT}/astrolabe175c/build/.android-flasher-build-incomplete"

usage() {
  echo "usage: $0 [--variant faculty|cyber] [--reuse-build] [--skip-lvgl-audit] [--install] [--adb-serial SERIAL]" >&2
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --variant)
      [[ $# -ge 2 ]] || { usage; exit 2; }
      VARIANT="$2"
      shift 2
      ;;
    --reuse-build)
      BUILD_FIRMWARE=0
      shift
      ;;
    --install)
      INSTALL=1
      shift
      ;;
    --skip-lvgl-audit)
      SKIP_LVGL_AUDIT=1
      shift
      ;;
    --adb-serial)
      [[ $# -ge 2 ]] || { usage; exit 2; }
      ADB_SERIAL="$2"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "error: unknown option: $1" >&2
      usage
      exit 2
      ;;
  esac
done

case "${VARIANT}" in
  faculty|cyber) ;;
  *) echo "error: variant must be faculty or cyber" >&2; exit 2 ;;
esac

find_idf() {
  local candidate
  if [[ -n "${IDF_PATH:-}" && -f "${IDF_PATH}/export.sh" ]]; then
    echo "${IDF_PATH}"
    return 0
  fi
  for candidate in \
    "${HOME}/esp/esp-idf-v5.5.1" \
    "${HOME}/esp/esp-idf" \
    "${HOME}/esp/esp-idf-v5.4"; do
    if [[ -f "${candidate}/export.sh" ]]; then
      echo "${candidate}"
      return 0
    fi
  done
  return 1
}

choose_note_edge() {
  local serial model match=""
  while IFS=$'\t' read -r serial state; do
    [[ "${state}" == "device" ]] || continue
    model="$(adb -s "${serial}" shell getprop ro.product.model 2>/dev/null | tr -d '\r')"
    case "${model}" in
      *SM-N915*)
        if [[ -n "${match}" ]]; then
          echo "error: more than one Galaxy Note Edge is connected; pass --adb-serial" >&2
          return 2
        fi
        match="${serial}"
        ;;
    esac
  done < <(adb devices | tail -n +2)
  if [[ -z "${match}" ]]; then
    echo "error: no Galaxy Note Edge found over ADB; connect wireless ADB or pass --adb-serial" >&2
    return 2
  fi
  echo "${match}"
}

if [[ "${BUILD_FIRMWARE}" == "1" ]]; then
  if ! IDF_PATH="$(find_idf)"; then
    echo "error: ESP-IDF was not found; set IDF_PATH to an installed ESP-IDF tree" >&2
    exit 2
  fi
  export IDF_PATH
  # shellcheck disable=SC1090
  source "${IDF_PATH}/export.sh"
  touch "${FAILED_BUILD_MARKER}"
  ASTROLABE175C_VARIANT="${VARIANT}" \
    ASTROLABE175C_SKIP_LVGL_AUDIT="${SKIP_LVGL_AUDIT}" \
    "${ROOT}/scripts/astrolabe175c_build.sh" build
else
  if [[ -f "${FAILED_BUILD_MARKER}" ]]; then
    echo "error: the previous firmware build did not complete; rebuild before using --reuse-build" >&2
    exit 2
  fi
fi

python3 "${ROOT}/scripts/android_flash_bridge.py" \
  --check \
  --variant "${VARIANT}" >/dev/null
rm -f "${FAILED_BUILD_MARKER}"

ANDROID_HOME="${ANDROID_HOME:-${HOME}/Library/Android/sdk}"
export ANDROID_HOME
if [[ ! -d "${ANDROID_HOME}" ]]; then
  echo "error: Android SDK not found at ${ANDROID_HOME}; set ANDROID_HOME" >&2
  exit 2
fi
if ! command -v gradle >/dev/null 2>&1; then
  echo "error: Gradle is not installed" >&2
  exit 2
fi

(cd "${APP_DIR}" && gradle --no-daemon :app:assembleDebug)
APK="${APP_DIR}/app/build/outputs/apk/debug/app-debug.apk"
echo "Android flasher APK: ${APK}"

if [[ "${INSTALL}" != "1" ]]; then
  echo "Firmware is bundled. Re-run with --install to update the Note Edge."
  exit 0
fi

command -v adb >/dev/null 2>&1 || { echo "error: adb is not installed" >&2; exit 2; }
if [[ -z "${ADB_SERIAL}" ]]; then
  ADB_SERIAL="$(choose_note_edge)"
fi
adb -s "${ADB_SERIAL}" get-state >/dev/null
MODEL="$(adb -s "${ADB_SERIAL}" shell getprop ro.product.model | tr -d '\r')"
case "${MODEL}" in
  *SM-N915*) ;;
  *) echo "error: ${ADB_SERIAL} is ${MODEL}, not a Galaxy Note Edge" >&2; exit 2 ;;
esac

OLD_PACKAGE_VERIFIER="$(adb -s "${ADB_SERIAL}" shell settings get global package_verifier_enable | tr -d '\r')"
OLD_ADB_VERIFIER="$(adb -s "${ADB_SERIAL}" shell settings get global verifier_verify_adb_installs | tr -d '\r')"
restore_verifier() {
  if [[ "${OLD_PACKAGE_VERIFIER}" == "null" ]]; then
    adb -s "${ADB_SERIAL}" shell settings delete global package_verifier_enable >/dev/null || true
  else
    adb -s "${ADB_SERIAL}" shell settings put global package_verifier_enable "${OLD_PACKAGE_VERIFIER}" >/dev/null || true
  fi
  if [[ "${OLD_ADB_VERIFIER}" == "null" ]]; then
    adb -s "${ADB_SERIAL}" shell settings delete global verifier_verify_adb_installs >/dev/null || true
  else
    adb -s "${ADB_SERIAL}" shell settings put global verifier_verify_adb_installs "${OLD_ADB_VERIFIER}" >/dev/null || true
  fi
}
trap restore_verifier EXIT
# Android 5's legacy verifier can block wireless ADB installs indefinitely.
# Disable it only for this install and restore the exact previous values on exit.
adb -s "${ADB_SERIAL}" shell settings put global package_verifier_enable 0
adb -s "${ADB_SERIAL}" shell settings put global verifier_verify_adb_installs 0
adb -s "${ADB_SERIAL}" install -r -t "${APK}"
restore_verifier
trap - EXIT

adb -s "${ADB_SERIAL}" shell am start \
  -n org.castaliainstitute.astrolabe.flasher/.MainActivity >/dev/null
echo "Installed and opened Astrolabe Flasher on ${MODEL} (${ADB_SERIAL})."
