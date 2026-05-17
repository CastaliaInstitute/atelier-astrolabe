#pragma once

#include <stdbool.h>

class Arduino_Canvas;

/** Rebuild LAN settings URL (mDNS or IP) when WiFi is up. */
void pm_settings_refresh_url(void);

/** URL encoded in the settings-face QR (`http://…/settings`). Empty if offline. */
const char *pm_settings_url_for_qr(void);

/** Short hostname label for UI (e.g. `astrolabe-a1b2.local`). */
const char *pm_settings_host_label(void);

bool pm_settings_draw_qr(Arduino_Canvas *gfx, int cx, int cy, int max_px);
