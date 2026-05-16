#pragma once

#include <stddef.h>

/**
 * Load WiFi SSID/password from NVS namespace "mynah".
 * If keys are missing, stores MYNAH_WIFI_NVS_DEFAULT_* then returns them.
 * If NVS cannot be opened, falls back to MYNAH_WIFI_SSID / MYNAH_WIFI_PASSWORD from build.
 * @return false if no usable SSID after all fallbacks.
 */
bool pm_wifi_credentials_load(char *ssid, size_t ssid_sz, char *pass, size_t pass_sz);
