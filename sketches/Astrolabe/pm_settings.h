#pragma once

/** Refresh mDNS/IP settings URL after WiFi comes up. */
void pm_settings_refresh_url(void);

/** Reset cached LAN host state after WiFi credentials change. */
void pm_settings_note_wifi_changed(void);

/** Full URL for the LAN settings page, or empty string when WiFi is down. */
const char *pm_settings_url(void);

/** Host label shown in the UI: astrolabe-xxxx.local when mDNS starts, else IP. */
const char *pm_settings_host_label(void);
