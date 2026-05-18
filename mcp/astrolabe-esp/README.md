# Astrolabe ESP MCP

Gives Cursor agents **serial console**, **JTAG/OpenOCD/GDB**, **PlatformIO build/upload** for the astrolabe watch.

## Setup

```bash
cd mcp/astrolabe-esp
./setup.sh
```

Add `astrolabe-esp` to **`~/.cursor/mcp.json`** (see `./install-cursor-mcp.sh` for the JSON block). Reload Cursor MCP.

## Console monitoring

| Tool | Use when |
|------|----------|
| **`astrolabe_serial_monitor`** | Firmware log on USB CDC (115200). Optional DTR reset, saves to `artifacts/monitor/`. |
| **`astrolabe_monitor`** | Alias for serial monitor. |
| **`astrolabe_jtag_openocd`** | OpenOCD adapter log only (link bring-up). |
| **`astrolabe_jtag_gdb`** | Halt / backtrace / inject via GDB (needs `waveshare_s3_175_debug` ELF). |
| **`astrolabe_console_monitor`** | Serial capture **and** short OpenOCD log in parallel. |
| **`astrolabe_debug_gdb`** | Low-level GDB batch (custom commands). |
| **`astrolabe_debug_set_face`** | JTAG: set `s_clock_face` (0–9 or `astro`, `synastry`, `hafez`, …) + repaint. |
| **`astrolabe_debug_inject_astro_boot`** | Same as `astrolabe_debug_set_face(4)`. |

**Important:** On ESP32-S3, **printf still goes to USB serial**, not JTAG. Use serial tools for console text; use JTAG tools to stop/inspect the CPU.

## Build / flash

| Tool | Purpose |
|------|---------|
| `astrolabe_list_ports` | Find `/dev/cu.usbmodem*` |
| `astrolabe_build` | `pio run` |
| `astrolabe_upload` | Flash firmware |
| `astrolabe_build_upload_monitor` | Upload + serial capture |

## Shell (no MCP)

```bash
./scripts/monitor_serial.sh          # live pio monitor
./scripts/monitor_capture.sh 60      # log to artifacts/monitor/
./scripts/monitor_jtag_gdb.sh        # pio debug
```

## Requirements

- PlatformIO (`pio` on PATH)
- Watch on USB (Espressif **303A:1001**)
- Debug ELF: `pio run -e waveshare_s3_175_debug` before GDB tools
