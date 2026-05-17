#!/usr/bin/env bash
# Set clock face over USB-JTAG (OpenOCD + GDB). Requires debug build on device.
# Usage: ./scripts/jtag_set_face.sh [face_index|name]
#   ./scripts/jtag_set_face.sh astro
#   ./scripts/jtag_set_face.sh 5
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FACE="${1:-4}"
cd "$ROOT"

BUILD_DIR="${PLATFORMIO_BUILD_DIR:-/tmp/astrolabe-pio-build}"
ELF="${BUILD_DIR}/waveshare_s3_175_debug/firmware.elf"
if [ ! -f "$ELF" ]; then
  echo "Building waveshare_s3_175_debug…"
  PIO_ENV=waveshare_s3_175_debug ./scripts/build.sh
fi

# Resolve name → index via MCP server helper
IDX="$("$ROOT/mcp/astrolabe-esp/.venv/bin/python" -c "
import sys
sys.path.insert(0, '$ROOT/mcp/astrolabe-esp')
from server import _resolve_face_index, FACE_LABELS
i = _resolve_face_index('$FACE')
print(i)
")"

GDB_SCRIPT="$ROOT/debug/.jtag_set_face_generated.gdb"
cat > "$GDB_SCRIPT" <<GDB
set pagination off
set remotetimeout 60
target extended-remote :3333
monitor gdb_memory_map disable
monitor reset halt
break loop
continue
set _ZL12s_clock_face = ${IDX}
set _ZL23g_clock_repaint_pending = 1
continue
detach
quit
GDB

echo "JTAG set face ${IDX} ($( "$ROOT/mcp/astrolabe-esp/.venv/bin/python" -c "
import sys; sys.path.insert(0, '$ROOT/mcp/astrolabe-esp')
from server import FACE_LABELS
print(FACE_LABELS[${IDX}])
" ))"

OPENOCD="$HOME/.platformio/packages/tool-openocd-esp32/bin/openocd"
SCRIPTS="$HOME/.platformio/packages/tool-openocd-esp32/share/openocd/scripts"
GDB_BIN="$HOME/.platformio/packages/toolchain-xtensa-esp32s3@8.4.0+2021r2-patch5/bin/xtensa-esp32s3-elf-gdb"
ELF="${BUILD_DIR}/waveshare_s3_175_debug/firmware.elf"

"$OPENOCD" -s "$SCRIPTS" -f board/esp32s3-builtin.cfg \
  -c "adapter speed 5000" -c "set ESP_FLASH_SIZE 16MB" -c "init" &
OC_PID=$!
sleep 3
"$GDB_BIN" -batch -x "$GDB_SCRIPT" "$ELF" || { kill $OC_PID 2>/dev/null; exit 1; }
kill $OC_PID 2>/dev/null || true
wait $OC_PID 2>/dev/null || true
