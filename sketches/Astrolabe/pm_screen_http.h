#pragma once

#include <Arduino.h>

class Arduino_Canvas;

/** Starts HTTP server on port 80 when WiFi is up (GET / and /screen.bmp). */
void pm_screen_http_begin(Arduino_Canvas *canvas);

/** Call from loop(); serves pending clients. */
void pm_screen_http_loop();
