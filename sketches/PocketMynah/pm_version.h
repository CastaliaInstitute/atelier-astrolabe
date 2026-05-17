#pragma once

#include <Arduino.h>

class Arduino_Canvas;

/** Draw Version face: branch, SHA, build date, QR → PM_BUILD_QR_URL. */
void pm_version_draw(Arduino_Canvas *gfx, void (*draw_centered)(const char *text, int y, uint16_t fg, uint8_t sx,
                                                               uint8_t sy));
