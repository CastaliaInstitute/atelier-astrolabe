#pragma once

#include <Arduino.h>

class PmDisplayCanvas;

/** Starts HTTP server on port 80 when WiFi is up (GET / and /screen.bmp). */
void pm_screen_http_begin(PmDisplayCanvas *canvas);

/** Call from loop(); serves pending clients. */
void pm_screen_http_loop();

/** Arm browser firmware uploads for a short physical-confirmation window. */
void pm_screen_http_ota_arm(uint32_t duration_ms);
bool pm_screen_http_ota_armed(void);
const char *pm_screen_http_ota_status(void);
size_t pm_screen_http_ota_bytes(void);
size_t pm_screen_http_ota_total(void);
const char *pm_screen_http_ota_integration_url(void);
/** Check the channel manifest and install a newer integration release if present. */
bool pm_screen_http_ota_auto_check(void);
