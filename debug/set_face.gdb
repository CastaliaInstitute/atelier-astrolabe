# Set clock face via JTAG (debug ELF). Usage:
#   pio run -e waveshare_s3_175_debug
#   ./scripts/jtag_set_face.sh 4
# Or: astrolabe_debug_set_face MCP tool
#
# Face indices: 0 ClassicAnalog, 1 Apocalypso, 2 DigitalLocal, 3 Spotify,
#               4 Astrology, 5 Moon, 6 CalciferCountdown, 7 Castalia, 8 Hafez

set pagination off
set remotetimeout 60
target extended-remote :3333
monitor gdb_memory_map disable
monitor reset halt
break loop
continue
set _ZL12s_clock_face = 4
set _ZL23g_clock_repaint_pending = 1
continue
detach
quit
