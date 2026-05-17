# JTAG / GDB debug (Astrolabe)

Use the **debug** PlatformIO env (`waveshare_s3_175_debug`) — same firmware with symbols and `MYNAH_DEBUG_GESTURES=1`.

## One-shot: Astrology TTS under debug

```bash
./scripts/debug_astro_session.sh 360
```

1. Builds and uploads **debug** ELF  
2. OpenOCD + GDB sets **Astrology** face (`s_clock_face = 4`, repaint)  
3. Serial monitor sends `astro` and logs until complete (watch for `voice: HTTP 200`, `voice: recv …`, `voice: message ok mp3=…`)

## Set face only (no reflash)

```bash
pio run -e waveshare_s3_175_debug   # once
./scripts/jtag_set_face.sh moon      # 0–7 or name
```

MCP: `astrolabe_debug_set_face(face="astro")`

## Interactive GDB

```bash
pio debug -e waveshare_s3_175_debug
```

Useful breakpoints:

| Symbol | When |
|--------|------|
| `voice_post_message_inner` | Text astrology / moon fortune POST |
| `read_http_json_body` | Large TTS JSON download |
| `pm_voice_poll` | Voice FSM completion |
| `loop` | Main loop (face inject scripts break here) |

## GDB symbols (static globals)

| Variable | Symbol |
|----------|--------|
| Current face | `_ZL12s_clock_face` |
| Force repaint | `_ZL23g_clock_repaint_pending` |

## Release vs debug

- **Release** (`waveshare_s3_175`): daily use, serial `face astro` / `astro`, smaller.  
- **Debug** (`waveshare_s3_175_debug`): JTAG face inject, GDB breakpoints, gesture banners on screen.

After JTAG `continue`, the CPU runs the **debug** image already on flash (upload debug ELF first).
