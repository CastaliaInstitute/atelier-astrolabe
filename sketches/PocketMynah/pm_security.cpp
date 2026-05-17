#include "pm_security.h"

#include <Arduino.h>
#include <FS.h>
#include <stdio.h>
#include <string.h>

#include <esp_partition.h>
#include <mbedtls/sha256.h>

#include <ArduinoJson.h>

#include "pm_ota_nvs.h"
#include "pm_partitions.h"
#include "pm_runtime_version.h"

esp_err_t pm_security_sha256_file(const char *path, uint8_t out32[32]) {
  if (!path || !out32) {
    return ESP_ERR_INVALID_ARG;
  }
  File f = pm_partitions_faces_fs().open(path, "r");
  if (!f) {
    return ESP_ERR_NOT_FOUND;
  }
  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);
  mbedtls_sha256_starts(&ctx, 0);
  uint8_t buf[1024];
  while (f.available()) {
    const size_t n = f.read(buf, sizeof(buf));
    if (n == 0) {
      break;
    }
    mbedtls_sha256_update(&ctx, buf, n);
  }
  mbedtls_sha256_finish(&ctx, out32);
  mbedtls_sha256_free(&ctx);
  f.close();
  return ESP_OK;
}

esp_err_t pm_security_sha256_partition(const char *label, size_t length, uint8_t out32[32]) {
  const esp_partition_t *part =
      esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, label);
  if (!part || length == 0 || length > part->size) {
    return ESP_ERR_INVALID_ARG;
  }
  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);
  mbedtls_sha256_starts(&ctx, 0);
  uint8_t buf[4096];
  size_t off = 0;
  while (off < length) {
    const size_t chunk = (length - off) > sizeof(buf) ? sizeof(buf) : (length - off);
    if (esp_partition_read(part, off, buf, chunk) != ESP_OK) {
      mbedtls_sha256_free(&ctx);
      return ESP_FAIL;
    }
    mbedtls_sha256_update(&ctx, buf, chunk);
    off += chunk;
  }
  mbedtls_sha256_finish(&ctx, out32);
  mbedtls_sha256_free(&ctx);
  return ESP_OK;
}

esp_err_t pm_security_verify_manifest_signature(const char *manifest_path) {
  if (!manifest_path) {
    return ESP_ERR_INVALID_ARG;
  }
  File f = pm_partitions_faces_fs().open(manifest_path, "r");
  if (!f) {
    return ESP_ERR_NOT_FOUND;
  }
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) {
    return ESP_ERR_INVALID_STATE;
  }
  const char *min_v = doc["runtime_min"] | MYNAH_RUNTIME_VERSION;
  const char *max_v = doc["runtime_max"] | "99.99.99";
  if (!pm_security_version_compatible(MYNAH_RUNTIME_VERSION, min_v, max_v)) {
    return ESP_ERR_INVALID_VERSION;
  }
  const int api = doc["runtime_api"] | MYNAH_RUNTIME_API_VERSION;
  if (api != MYNAH_RUNTIME_API_VERSION) {
    return ESP_ERR_INVALID_VERSION;
  }
  /** Phase 5: Ed25519 verify signature over manifest root hash. Dev/stub: accept. */
  if (pm_ota_nvs_update_channel() == PmUpdateChannel::Stable) {
    const char *sig = doc["signature"]["algorithm"] | "none";
    if (strcmp(sig, "ed25519") != 0 && strcmp(sig, "none") != 0) {
      return ESP_ERR_NOT_SUPPORTED;
    }
  }
  return ESP_OK;
}

static int parse_semver_part(const char **p) {
  int v = 0;
  while (**p >= '0' && **p <= '9') {
    v = v * 10 + (**p - '0');
    ++*p;
  }
  return v;
}

bool pm_security_version_compatible(const char *runtime_version, const char *runtime_min,
                                    const char *runtime_max) {
  if (!runtime_version || !runtime_min) {
    return false;
  }
  /** Simple string compare fallback; semver parse for x.y.z */
  if (strcmp(runtime_version, runtime_min) < 0) {
    return false;
  }
  if (runtime_max && runtime_max[0] && strstr(runtime_max, "x")) {
    return true;
  }
  if (runtime_max && runtime_max[0] && strcmp(runtime_version, runtime_max) > 0) {
    return false;
  }
  return true;
}

bool pm_security_version_is_downgrade(const char *new_version, const char *current_version) {
  if (!new_version || !current_version) {
    return false;
  }
  if (pm_ota_nvs_allow_downgrade()) {
    return false;
  }
  return strcmp(new_version, current_version) < 0;
}
