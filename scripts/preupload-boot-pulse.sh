#!/usr/bin/env bash
# DTR pulse so ESP32-S3 USB-CDC may enter the ROM bootloader (best-effort).
set -euo pipefail
PORT="${1:?port}"
PY="${ASTROLABE_CI_VENV:-${HOME}/.astrolabe-ci-venv}/bin/python"
if [[ ! -x "$PY" ]]; then
  PY="$(command -v python3)"
fi
"$PY" -c "
import serial, sys, time
port = sys.argv[1]
ser = serial.Serial(port, 115200, timeout=0.25)
ser.setDTR(False); time.sleep(0.08)
ser.setDTR(True); time.sleep(0.08)
ser.setDTR(False); time.sleep(0.5)
ser.close()
" "$PORT"
