#!/usr/bin/env bash
# Build, upload, and attach GDB (esp-builtin USB-JTAG) for waveshare_s3_175_debug.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
ENV=waveshare_s3_175_debug
PORT="${ASTROLABE_SERIAL_PORT:-${MYNAH_SERIAL_PORT:-}}"

if [ -z "$PORT" ]; then
  PORT=$(pio device list 2>/dev/null | awk '/usbmodem/{print $1; exit}')
fi
if [ -z "$PORT" ]; then
  echo "No ESP32 USB port (303A:1001). Plug in the watch and retry." >&2
  exit 1
fi

echo "Using port $PORT"
pio run -e "$ENV" -t upload --upload-port "$PORT"

ELF=".pio/build/${ENV}/firmware.elf"
GDB="${HOME}/.platformio/packages/toolchain-xtensa-esp32s3@8.4.0+2021r2-patch5/bin/xtensa-esp32s3-elf-gdb"
OPENOCD="${HOME}/.platformio/packages/tool-openocd-esp32/bin/openocd"
SCRIPTS="${HOME}/.platformio/packages/tool-openocd-esp32/share/openocd/scripts"

kill_openocd() { kill "$OC_PID" 2>/dev/null || true; }
trap kill_openocd EXIT

"$OPENOCD" -s "$SCRIPTS" -f board/esp32s3-builtin.cfg \
  -c "adapter speed 5000" \
  -c "set ESP_FLASH_SIZE 16MB" \
  -c "init" -c "reset halt" \
  >/tmp/pm_openocd.log 2>&1 &
OC_PID=$!
sleep 2

if ! grep -q "Listening on port 3333" /tmp/pm_openocd.log 2>/dev/null; then
  echo "OpenOCD failed — see /tmp/pm_openocd.log" >&2
  tail -20 /tmp/pm_openocd.log >&2
  exit 1
fi

echo "OpenOCD on :3333 — GDB attaching (Ctrl+C to quit)"
exec "$GDB" -q "$ELF" \
  -ex "set remotetimeout 60" \
  -ex "target extended-remote :3333" \
  -ex "monitor gdb_memory_map disable" \
  -ex "tbreak setup" \
  -ex "continue"
