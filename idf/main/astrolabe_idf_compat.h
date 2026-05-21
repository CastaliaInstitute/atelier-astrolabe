#pragma once

#include "esp_system.h"
#include "sdkconfig.h"

#if !CONFIG_TINYUSB_ENABLED
typedef enum {
  RESTART_NO_PERSIST,
  RESTART_PERSIST,
  RESTART_BOOTLOADER,
  RESTART_BOOTLOADER_DFU,
  RESTART_TYPE_MAX
} restart_type_t;

static inline void usb_persist_restart(restart_type_t mode) {
  (void)mode;
  esp_restart();
}
#endif
