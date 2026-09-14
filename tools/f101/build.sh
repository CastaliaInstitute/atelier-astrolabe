#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
export IDF_PATH="${IDF_PATH:-/root/esp/esp-idf}"
export IDF_PYTHON_ENV_PATH="${IDF_PYTHON_ENV_PATH:-/root/.espressif/python_env/idf5.5_py3.13_env}"
export PATH="${IDF_PYTHON_ENV_PATH}/bin:${PATH}"
source "$IDF_PATH/export.sh"
cd "$ROOT/astrolabe185b"
args=(-D ASTROLABE185B_BUILD_VARIANT=cyber -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.f101.defaults')
if [[ ! -f sdkconfig ]]; then
  "$IDF_PYTHON_ENV_PATH/bin/python" "$IDF_PATH/tools/idf.py" "${args[@]}" set-target esp32s3
fi
"$IDF_PYTHON_ENV_PATH/bin/python" - <<'PY'
from pathlib import Path
config = Path('sdkconfig').read_text().splitlines()
required = ['CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y',
            'CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions_16mb.csv"',
            'CONFIG_BT_NIMBLE_ENABLED=y', 'CONFIG_SPIRAM=y',
            'CONFIG_TINYUSB_CDC_ENABLED=y', 'CONFIG_TINYUSB_NET_MODE_NCM=y',
            'CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y',
            'CONFIG_ESP_MAIN_TASK_STACK_SIZE=16384',
            'CONFIG_ESP_SYSTEM_EVENT_TASK_STACK_SIZE=8192']
missing = [setting for setting in required if setting not in config]
if missing:
    raise SystemExit('Incompatible 1.85B sdkconfig: ' + ', '.join(missing))
PY
exec "$IDF_PYTHON_ENV_PATH/bin/python" "$IDF_PATH/tools/idf.py" "${args[@]}" build
