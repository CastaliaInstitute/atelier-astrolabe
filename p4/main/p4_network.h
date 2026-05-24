#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  bool initialized;
  bool has_credentials;
  bool enabled;
  bool connected;
  char ssid[33];
  char ip[16];
  char hostname[32];
  int rssi;
} astrolabe_p4_network_status_t;

esp_err_t astrolabe_p4_network_init(void);
bool astrolabe_p4_network_start(void);
bool astrolabe_p4_network_stop(void);
bool astrolabe_p4_network_save_credentials(const char *ssid, const char *pass);
bool astrolabe_p4_network_forget_credentials(void);
void astrolabe_p4_network_log_status(void);
void astrolabe_p4_network_scan(void);
astrolabe_p4_network_status_t astrolabe_p4_network_status(void);

#ifdef __cplusplus
}
#endif
