#pragma once

#include <Arduino.h>

class PmDisplayCanvas;

/** Starts HTTP server on port 80 when WiFi is up (GET / and /screen.bmp). */
void pm_screen_http_begin(PmDisplayCanvas *canvas);

/** Call from loop(); serves pending clients. */
void pm_screen_http_loop();

/** Streams the current framebuffer as base64 BMP over Serial for USB-only QA. */
bool pm_screen_http_serial_screen64();
