#!/usr/bin/env bash
# Apply idf/ patches to managed_components (idempotent).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IDF_PROJECT="${ROOT}/idf"
ARDUINO_ROOT="${IDF_PROJECT}/managed_components/espressif__arduino-esp32"

if [[ ! -d "${ARDUINO_ROOT}" ]]; then
  echo "idf_apply_patches: skip (managed_components not fetched yet)" >&2
  exit 0
fi

ble_device="${ARDUINO_ROOT}/libraries/BLE/src/BLEDevice.cpp"
if [[ -f "${ble_device}" ]] && ! grep -q 'ASTROLABE_IDF_BLE_IRK_SHIM' "${ble_device}"; then
  perl -i -pe '
    if ($. == 1 .. eof) {
      s/^\s*esp_err_t ret = esp_ble_gap_get_local_irk\(irk\);$/  \/* ASTROLABE_IDF_BLE_IRK_SHIM *\/\n  memset(irk, 0, 16);\n  esp_err_t ret = ESP_OK;/;
    }
  ' "${ble_device}" 2>/dev/null || true
  if grep -q 'esp_ble_gap_get_local_irk' "${ble_device}"; then
    # Fallback when line numbers differ across arduino-esp32 releases.
    sed -i.bak \
      's/esp_err_t ret = esp_ble_gap_get_local_irk(irk);/memset(irk, 0, 16); esp_err_t ret = ESP_OK; \/* ASTROLABE_IDF_BLE_IRK_SHIM *\//' \
      "${ble_device}"
    rm -f "${ble_device}.bak"
  fi
  echo "idf_apply_patches: patched BLEDevice.cpp (local IRK shim)" >&2
fi
