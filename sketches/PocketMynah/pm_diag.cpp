#include "pm_diag.h"

#include <Arduino.h>
#include <stdio.h>
#include <string.h>

#include <esp_ota_ops.h>

#include "pm_faces_pack.h"
#include "pm_ota_nvs.h"
#include "pm_partitions.h"

static bool s_safe_mode = false;
static char s_safe_reason[96] = "";
static bool s_runtime_marked = false;

void pm_diag_init(void) {
  pm_ota_nvs_begin();
  pm_diag_record_boot();

  esp_ota_img_states_t st = ESP_OTA_IMG_VALID;
  if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &st) == ESP_OK &&
      st == ESP_OTA_IMG_PENDING_VERIFY) {
    Serial.println("[diag] runtime pending verification");
  }

  if (pm_ota_nvs_face_pending()) {
    const uint8_t tries = pm_ota_nvs_face_boot_attempts() + 1;
    pm_ota_nvs_face_set_boot_attempts(tries);
    Serial.printf("[diag] face pack pending boot try %u\n", tries);
    if (tries > kPmFaceBootAttemptsMax) {
      const char *prev = pm_ota_nvs_face_previous_partition();
      pm_ota_nvs_face_set_active_partition(prev);
      pm_ota_nvs_face_set_pending(false);
      pm_ota_nvs_face_set_boot_attempts(0);
      pm_partitions_mount_faces_label(prev);
      Serial.printf("[diag] face pack rollback -> %s\n", prev);
    }
  }

  if (!pm_partitions_mount_all()) {
    pm_diag_enter_safe_mode("partition mount failed");
    return;
  }

  /** Missing or empty face pack is OK — builtin clock faces still run. */
  if (!pm_faces_pack_init()) {
    Serial.printf("[diag] face pack optional: %s\n", pm_faces_pack_last_error());
  }
}

bool pm_diag_safe_mode(void) { return s_safe_mode; }

void pm_diag_enter_safe_mode(const char *reason) {
  s_safe_mode = true;
  if (reason) {
    strncpy(s_safe_reason, reason, sizeof(s_safe_reason) - 1);
    s_safe_reason[sizeof(s_safe_reason) - 1] = '\0';
  } else {
    s_safe_reason[0] = '\0';
  }
  Serial.printf("[diag] SAFE MODE: %s\n", s_safe_reason);
}

const char *pm_diag_safe_mode_reason(void) { return s_safe_reason; }

void pm_diag_record_boot(void) {}

void pm_diag_mark_runtime_valid(void) {
  if (s_runtime_marked) {
    return;
  }
  const esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
  Serial.printf("[diag] runtime marked valid: %s\n", esp_err_to_name(err));
  s_runtime_marked = true;
}

void pm_diag_mark_facepack_valid(void) {
  pm_ota_nvs_face_set_pending(false);
  pm_ota_nvs_face_set_boot_attempts(0);
  pm_ota_nvs_face_set_previous_partition(pm_ota_nvs_face_active_partition());
}

bool pm_diag_runtime_pending_validation(void) {
  esp_ota_img_states_t st = ESP_OTA_IMG_VALID;
  if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &st) != ESP_OK) {
    return false;
  }
  return st == ESP_OTA_IMG_PENDING_VERIFY;
}

bool pm_diag_facepack_pending(void) { return pm_ota_nvs_face_pending(); }

void pm_diag_on_face_boot(void) {}

void pm_diag_record_face_error(esp_err_t err) {
  Serial.printf("[diag] face error %s\n", esp_err_to_name(err));
}

void pm_diag_status_line(char *buf, size_t cap) {
  if (!buf || cap == 0) {
    return;
  }
  const esp_app_desc_t *app = esp_ota_get_app_description();
  snprintf(buf, cap, "rt %s face %s%s", app ? app->version : "?", pm_ota_nvs_face_active_version(),
           s_safe_mode ? " SAFE" : "");
}
