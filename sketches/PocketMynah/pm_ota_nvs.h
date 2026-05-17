#pragma once

#include <stddef.h>
#include <stdint.h>

/** NVS namespace `mynah_ota` — separate from WiFi `mynah`. */
static constexpr const char *kPmOtaNvsNs = "mynah_ota";

static constexpr const char *kPmFacePartA = "faces_a";
static constexpr const char *kPmFacePartB = "faces_b";

static constexpr uint8_t kPmFaceBootAttemptsMax = 3;

enum class PmUpdateChannel : uint8_t {
  Stable = 0,
  Beta,
  Dev,
  Factory,
};

void pm_ota_nvs_begin(void);

const char *pm_ota_nvs_face_active_partition(void);
void pm_ota_nvs_face_set_active_partition(const char *label);

const char *pm_ota_nvs_face_previous_partition(void);
void pm_ota_nvs_face_set_previous_partition(const char *label);

const char *pm_ota_nvs_face_active_version(void);
void pm_ota_nvs_face_set_active_version(const char *ver);

const char *pm_ota_nvs_face_pending_version(void);
void pm_ota_nvs_face_set_pending_version(const char *ver);

bool pm_ota_nvs_face_pending(void);
void pm_ota_nvs_face_set_pending(bool pending);

uint8_t pm_ota_nvs_face_boot_attempts(void);
void pm_ota_nvs_face_set_boot_attempts(uint8_t n);

const char *pm_ota_nvs_face_last_good_id(void);
void pm_ota_nvs_face_set_last_good_id(const char *id);

PmUpdateChannel pm_ota_nvs_update_channel(void);
void pm_ota_nvs_set_update_channel(PmUpdateChannel ch);

bool pm_ota_nvs_allow_downgrade(void);
void pm_ota_nvs_set_allow_downgrade(bool allow);

int64_t pm_ota_nvs_last_check_epoch(void);
void pm_ota_nvs_set_last_check_epoch(int64_t epoch);

const char *pm_ota_nvs_inactive_face_partition(void);
