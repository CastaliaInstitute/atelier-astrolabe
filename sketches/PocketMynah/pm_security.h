#pragma once

#include <stddef.h>
#include <stdint.h>

#include <esp_err.h>

esp_err_t pm_security_sha256_file(const char *path, uint8_t out32[32]);
esp_err_t pm_security_sha256_partition(const char *label, size_t length, uint8_t out32[32]);

esp_err_t pm_security_verify_manifest_signature(const char *manifest_path);

bool pm_security_version_compatible(const char *runtime_version, const char *runtime_min,
                                    const char *runtime_max);
bool pm_security_version_is_downgrade(const char *new_version, const char *current_version);
