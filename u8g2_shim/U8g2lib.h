#ifndef U8G2LIB_HH
#define U8G2LIB_HH

/**
 * Minimal U8g2lib.h so Arduino_GFX sees `__has_include(<U8g2lib.h>)` when compiling
 * GFX sources (add `-Iu8g2_shim` in platformio.ini). Full olikraus/U8g2 is not linked;
 * UTF-8 glyph decode is implemented in Arduino_GFX.cpp; font tables live in GFX headers.
 */

#ifndef U8X8_FONT_SECTION
#define U8X8_FONT_SECTION(name)
#endif
#ifndef U8G2_FONT_SECTION
#define U8G2_FONT_SECTION(name)
#endif

#endif
