# Astrology TTS + simulated button presses over JTAG.
#
# 1. Flash debug env:  pio run -e waveshare_s3_175_debug -t upload
# 2. Start debug:      pio debug -e waveshare_s3_175_debug
#    Or Cursor: Run → "Astrolabe JTAG (astro TTS)"
# 3. In GDB (target must be halted — breakpoint in loop() or Ctrl+C):
#
#    set g_clock_face = 4          # ClockFace::Astrology (enum value; verify in Astrolabe.ino)
#    set s_debug_inject_edge = 1   # simulate BOOT tap → astro text reading
#    continue
#
#    set s_debug_pwr_hold = 1      # simulate PWR hold → STT (wait ~400ms in loop)
#    continue
#    set s_debug_pwr_hold = 0
#    continue
#
#    call pm_debug_tap_boot()
#    continue

set pagination off
set remotetimeout 30

break astro_tts_debug_hook
break pm_debug_tap_boot
break pm_speaker_play_begin

commands 1
  silent
  printf "astro_tts stage=%u\n", $a0
  continue
end

tbreak setup
continue
