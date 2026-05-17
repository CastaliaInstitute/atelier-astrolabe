#pragma once

#include <Arduino.h>

bool pm_wifi_begin();
bool pm_wifi_connected();
void pm_ntp_sync_blocking();
/** If WiFi is up but clock never set, call occasionally (e.g. once per minute) to re-run SNTP. */
void pm_ntp_retry_if_stale();
bool pm_time_valid();
void pm_time_utc(struct tm *out_tm);
/** UTC plus IP-derived offset (see pm_geo_tz_refresh_from_ip). */
void pm_time_local(struct tm *out_tm);
