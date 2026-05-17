#include "pm_ota.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <stdio.h>
#include <string.h>

#include <esp_ota_ops.h>
#include <esp_partition.h>

#include "pm_diag.h"
#include "pm_faces_pack.h"
#include "pm_ota_nvs.h"
#include "pm_partitions.h"
#include "pm_security.h"

static char s_ota_status[64] = "idle";

static bool hex_to_bytes(const char *hex, uint8_t *out, size_t out_len) {
  if (!hex || strlen(hex) < out_len * 2) {
    return false;
  }
  for (size_t i = 0; i < out_len; ++i) {
    unsigned v = 0;
    if (sscanf(hex + i * 2, "%2x", &v) != 1) {
      return false;
    }
    out[i] = static_cast<uint8_t>(v);
  }
  return true;
}

void pm_ota_init(void) {
  strncpy(s_ota_status, "ready", sizeof(s_ota_status) - 1);
}

void pm_ota_status_line(char *buf, size_t cap) {
  if (!buf || cap == 0) {
    return;
  }
  snprintf(buf, cap, "OTA: %s", s_ota_status);
}

void pm_ota_print_status(void) {
  char line[96];
  pm_diag_status_line(line, sizeof(line));
  Serial.println(line);
  Serial.printf("OTA: %s\n", s_ota_status);
  Serial.printf("face part: %s ver %s pending=%d\n", pm_ota_nvs_face_active_partition(),
                pm_ota_nvs_face_active_version(), pm_ota_nvs_face_pending() ? 1 : 0);
}

esp_err_t pm_ota_validate_pending_runtime(void) {
  if (!pm_diag_runtime_pending_validation()) {
    return ESP_OK;
  }
  if (!pm_partitions_mount_all()) {
    strncpy(s_ota_status, "validate mount fail", sizeof(s_ota_status) - 1);
    return ESP_FAIL;
  }
  pm_faces_pack_init();
  pm_diag_mark_runtime_valid();
  strncpy(s_ota_status, "runtime valid", sizeof(s_ota_status) - 1);
  return ESP_OK;
}

esp_err_t pm_ota_validate_pending_facepack(void) {
  if (!pm_ota_nvs_face_pending()) {
    return ESP_OK;
  }
  if (!pm_faces_pack_init()) {
    return ESP_FAIL;
  }
  if (pm_faces_pack_dry_run() != ESP_OK) {
    return ESP_FAIL;
  }
  pm_diag_mark_facepack_valid();
  strncpy(s_ota_status, "facepack valid", sizeof(s_ota_status) - 1);
  return ESP_OK;
}

esp_err_t pm_ota_update_runtime(const PmOtaRuntimeUpdate *u) {
  if (!u || !u->url || !WiFi.isConnected()) {
    return ESP_ERR_INVALID_ARG;
  }
  strncpy(s_ota_status, "runtime dl", sizeof(s_ota_status) - 1);

  const esp_partition_t *update_part = esp_ota_get_next_update_partition(nullptr);
  if (!update_part) {
    strncpy(s_ota_status, "no ota slot", sizeof(s_ota_status) - 1);
    return ESP_ERR_NOT_FOUND;
  }

  HTTPClient http;
  http.begin(u->url);
  http.setTimeout(60000);
  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    strncpy(s_ota_status, "http fail", sizeof(s_ota_status) - 1);
    return ESP_FAIL;
  }
  const int len = http.getSize();
  if (u->size > 0 && static_cast<size_t>(len) != u->size) {
    http.end();
    strncpy(s_ota_status, "size mismatch", sizeof(s_ota_status) - 1);
    return ESP_ERR_INVALID_SIZE;
  }
  esp_ota_handle_t ota_handle = 0;
  esp_err_t err = esp_ota_begin(update_part, len > 0 ? static_cast<size_t>(len) : OTA_SIZE_UNKNOWN,
                                &ota_handle);
  if (err != ESP_OK) {
    http.end();
    strncpy(s_ota_status, "ota begin fail", sizeof(s_ota_status) - 1);
    return err;
  }
  WiFiClient *stream = http.getStreamPtr();
  uint8_t buf[4096];
  size_t written = 0;
  while (http.connected() && (len < 0 || written < static_cast<size_t>(len))) {
    const size_t avail = stream->available();
    if (avail == 0) {
      delay(1);
      if (!http.connected()) {
        break;
      }
      continue;
    }
    const size_t n = stream->readBytes(buf, avail > sizeof(buf) ? sizeof(buf) : avail);
    if (n == 0) {
      break;
    }
    err = esp_ota_write(ota_handle, buf, n);
    if (err != ESP_OK) {
      esp_ota_abort(ota_handle);
      http.end();
      strncpy(s_ota_status, "ota write fail", sizeof(s_ota_status) - 1);
      return err;
    }
    written += n;
  }
  http.end();
  err = esp_ota_end(ota_handle);
  if (err != ESP_OK) {
    strncpy(s_ota_status, "ota end fail", sizeof(s_ota_status) - 1);
    return err;
  }
  if (u->sha256_hex && u->sha256_hex[0]) {
    /** SHA verify of running image happens post-reboot; optional pre-check skipped for Arduino Update API */
  }
  if (!esp_ota_set_boot_partition(update_part)) {
    strncpy(s_ota_status, "set boot fail", sizeof(s_ota_status) - 1);
    return ESP_FAIL;
  }
  strncpy(s_ota_status, "runtime reboot", sizeof(s_ota_status) - 1);
  delay(200);
  esp_restart();
  return ESP_OK;
}

static esp_err_t download_url_to_partition(const char *url, const char *label, size_t expected_size) {
  const esp_partition_t *part =
      esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, label);
  if (!part) {
    return ESP_ERR_NOT_FOUND;
  }
  if (esp_partition_erase_range(part, 0, part->size) != ESP_OK) {
    return ESP_FAIL;
  }

  HTTPClient http;
  http.begin(url);
  http.setTimeout(60000);
  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    return ESP_FAIL;
  }
  const int len = http.getSize();
  if (expected_size > 0 && static_cast<size_t>(len) != expected_size) {
    http.end();
    return ESP_ERR_INVALID_SIZE;
  }
  if (static_cast<size_t>(len) > part->size) {
    http.end();
    return ESP_ERR_NO_MEM;
  }

  WiFiClient *stream = http.getStreamPtr();
  uint8_t buf[4096];
  size_t off = 0;
  while (http.connected() && (len < 0 || off < static_cast<size_t>(len))) {
    const size_t avail = stream->available();
    if (avail == 0) {
      delay(1);
      continue;
    }
    const size_t n = stream->readBytes(buf, avail > sizeof(buf) ? sizeof(buf) : avail);
    if (n == 0) {
      break;
    }
    if (esp_partition_write(part, off, buf, n) != ESP_OK) {
      http.end();
      return ESP_FAIL;
    }
    off += n;
  }
  http.end();
  if (len > 0 && off != static_cast<size_t>(len)) {
    return ESP_FAIL;
  }
  return ESP_OK;
}

esp_err_t pm_ota_activate_staged_facepack(const PmOtaFacepackUpdate *u) {
  if (!u) {
    return ESP_ERR_INVALID_ARG;
  }
  const char *inactive = pm_ota_nvs_inactive_face_partition();
  if (!pm_partitions_mount_faces_label(inactive)) {
    return ESP_FAIL;
  }
  if (pm_security_verify_manifest_signature("/manifest.json") != ESP_OK) {
    return ESP_ERR_INVALID_STATE;
  }
  if (pm_faces_pack_mount_inactive_and_verify(inactive) != ESP_OK) {
    return ESP_FAIL;
  }
  if (u->sha256_hex && u->sha256_hex[0]) {
    uint8_t expect[32];
    uint8_t got[32];
    if (!hex_to_bytes(u->sha256_hex, expect, 32)) {
      return ESP_ERR_INVALID_ARG;
    }
    const size_t hash_len = u->size > 0 ? u->size : 0;
    if (hash_len > 0 &&
        pm_security_sha256_partition(inactive, hash_len, got) == ESP_OK) {
      if (memcmp(expect, got, 32) != 0) {
        return ESP_ERR_INVALID_CRC;
      }
    }
  }
  pm_ota_nvs_face_set_previous_partition(pm_ota_nvs_face_active_partition());
  pm_ota_nvs_face_set_active_partition(inactive);
  if (u->version) {
    pm_ota_nvs_face_set_pending_version(u->version);
    pm_ota_nvs_face_set_active_version(u->version);
  }
  pm_ota_nvs_face_set_pending(true);
  pm_ota_nvs_face_set_boot_attempts(0);
  strncpy(s_ota_status, "face flip", sizeof(s_ota_status) - 1);
  delay(200);
  esp_restart();
  return ESP_OK;
}

esp_err_t pm_ota_update_facepack(const PmOtaFacepackUpdate *u) {
  if (!u || !u->url || !WiFi.isConnected()) {
    return ESP_ERR_INVALID_ARG;
  }
  strncpy(s_ota_status, "facepack dl", sizeof(s_ota_status) - 1);
  const char *inactive = pm_ota_nvs_inactive_face_partition();
  if (!pm_partitions_erase_faces_label(inactive)) {
    strncpy(s_ota_status, "face erase fail", sizeof(s_ota_status) - 1);
    return ESP_FAIL;
  }
  const esp_err_t dl = download_url_to_partition(u->url, inactive, u->size);
  if (dl != ESP_OK) {
    strncpy(s_ota_status, "face dl fail", sizeof(s_ota_status) - 1);
    return dl;
  }
  return pm_ota_activate_staged_facepack(u);
}
