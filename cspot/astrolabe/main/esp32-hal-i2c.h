#pragma once

#include <stddef.h>
#include <stdint.h>

#include "driver/i2c.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

static inline esp_err_t i2cWrite(uint8_t i2c_num, uint16_t address, const uint8_t *buff, size_t size,
                                 uint32_t timeOutMillis) {
  return i2c_master_write_to_device((i2c_port_t)i2c_num, address, buff, size, pdMS_TO_TICKS(timeOutMillis));
}

static inline esp_err_t i2cRead(uint8_t i2c_num, uint16_t address, uint8_t *buff, size_t size,
                                uint32_t timeOutMillis, size_t *readCount) {
  esp_err_t err = i2c_master_read_from_device((i2c_port_t)i2c_num, address, buff, size, pdMS_TO_TICKS(timeOutMillis));
  if (readCount) {
    *readCount = (err == ESP_OK) ? size : 0;
  }
  return err;
}

#ifdef __cplusplus
}
#endif
