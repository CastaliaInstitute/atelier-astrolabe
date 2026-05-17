#pragma once

#include <stddef.h>
#include <stdint.h>

#include <esp_err.h>

struct PmOtaRuntimeUpdate {
  const char *url;
  const char *sha256_hex;
  size_t size;
};

struct PmOtaFacepackUpdate {
  const char *url;
  const char *sha256_hex;
  size_t size;
  const char *version;
};

void pm_ota_init(void);

void pm_ota_status_line(char *buf, size_t cap);
void pm_ota_print_status(void);

esp_err_t pm_ota_validate_pending_runtime(void);
esp_err_t pm_ota_validate_pending_facepack(void);

esp_err_t pm_ota_update_runtime(const PmOtaRuntimeUpdate *u);
esp_err_t pm_ota_update_facepack(const PmOtaFacepackUpdate *u);

/** Apply staged face pack: verify inactive partition, flip NVS, restart. */
esp_err_t pm_ota_activate_staged_facepack(const PmOtaFacepackUpdate *u);
