#!/usr/bin/env bash
# Halt via OpenOCD, inject astrology BOOT tap, capture USB serial for voice-pipeline logs.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PORT="${ASTROLABE_SERIAL_PORT:-${MYNAH_SERIAL_PORT:-/dev/cu.usbmodem1101}}"
ELF="$ROOT/.pio/build/waveshare_s3_175_debug/firmware.elf"
GDB="${HOME}/.platformio/packages/toolchain-xtensa-esp32s3@8.4.0+2021r2-patch5/bin/xtensa-esp32s3-elf-gdb"
OPENOCD_BIN="${HOME}/.platformio/packages/tool-openocd-esp32/bin/openocd"
OPENOCD_SCRIPTS="${HOME}/.platformio/packages/tool-openocd-esp32/share/openocd/scripts"
PY="${PY:-/opt/homebrew/bin/python3.11}"

if [[ ! -f "$ELF" ]]; then
  echo "Missing $ELF — run: pio run -e waveshare_s3_175_debug" >&2
  exit 1
fi

LOG="$(mktemp /tmp/pm_serialXXXX.log)"
cleanup() { kill "$OC_PID" 2>/dev/null || true; }
trap cleanup EXIT

"$OPENOCD_BIN" -s "$OPENOCD_SCRIPTS" \
  -f board/esp32s3-builtin.cfg \
  -c "adapter speed 5000" \
  -c "set ESP_FLASH_SIZE 16MB" \
  >/tmp/openocd_pm.log 2>&1 &
OC_PID=$!
sleep 3

"$PY" -u - "$PORT" "$LOG" <<'PY' &
import serial, sys, time
port, logpath = sys.argv[1], sys.argv[2]
ser = serial.Serial(port, 115200, timeout=0.2)
ser.reset_input_buffer()
lines = []
t0 = time.time()
with open(logpath, "w") as f:
    while time.time() - t0 < 120:
        chunk = ser.read(16384)
        if not chunk:
            continue
        text = chunk.decode("utf-8", errors="replace")
        f.write(text)
        f.flush()
        for line in text.splitlines():
            line = line.strip()
            if line:
                lines.append(line)
                print(line, flush=True)
ser.close()
PY
CAP_PID=$!
sleep 0.5

"$GDB" -batch -x "$ROOT/debug/trigger_astro_boot.gdb" "$ELF" 2>/tmp/gdb_pm.log || true

wait "$CAP_PID" 2>/dev/null || true
echo "--- gdb ---"
tail -20 /tmp/gdb_pm.log
echo "--- voice/HTTP lines ---"
grep -E 'pm_voice|astro_tts|voice-pipeline|HTTP|bad response|empty reply|mp3|truncat|timeout|401' "$LOG" || true
echo "--- log file: $LOG ---"
