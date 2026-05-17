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
