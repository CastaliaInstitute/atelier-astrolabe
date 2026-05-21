#pragma once

#include <Arduino.h>

bool pm_wifi_begin();
/** Disconnect and reconnect using credentials currently stored in NVS/build config. */
bool pm_wifi_reconnect();
const char *pm_wifi_hostname();
const char *pm_wifi_mdns_name();
const char *pm_wifi_mac_suffix();
const char *pm_wifi_mac_string();
/**
 * Required before starting the BT controller while WiFi is up (Radar / presence BLE).
 * Enables WiFi modem sleep (WIFI_PS_MIN_MODEM) so WiFi + BLE coexistence does not abort().
 */
void pm_wifi_enable_bt_coexistence(void);
/** Temporarily stop WiFi/mDNS to free internal heap for Radar BLE startup. */
void pm_wifi_pause_for_ble(void);
/** Resume WiFi after Radar BLE has been stopped. */
void pm_wifi_resume_after_ble(void);
/** Detect connect/disconnect; plays connect chime once per link-up. Call from loop(). */
void pm_wifi_poll(void);
bool pm_wifi_connected();
void pm_ntp_sync_blocking();
/** If WiFi is up but clock never set, call occasionally (e.g. once per minute) to re-run SNTP. */
void pm_ntp_retry_if_stale();
bool pm_time_valid();
void pm_time_utc(struct tm *out_tm);
/** UTC plus IP-derived offset (see pm_geo_tz_refresh_from_ip). */
void pm_time_local(struct tm *out_tm);
