#include "pm_partitions.h"

#include <FFat.h>
#include <LittleFS.h>

#include <esp_partition.h>

#include "pm_ota_nvs.h"

static fs::LittleFSFS s_faces_fs;
static fs::LittleFSFS s_assets_fs;
static bool s_faces_mounted = false;
static bool s_assets_mounted = false;
static bool s_common_mounted = false;
static bool s_ephem_mounted = false;
static bool s_crash_mounted = false;
static char s_faces_label[16] = "";

static bool mount_littlefs(fs::LittleFSFS &fs, const char *label, const char *base_path) {
  if (fs.begin(false, base_path, 5, label)) {
    return true;
  }
  return fs.begin(true, base_path, 5, label);
}

bool pm_partitions_mount_all(void) {
  const char *face_label = pm_ota_nvs_face_active_partition();
  strncpy(s_faces_label, face_label, sizeof(s_faces_label) - 1);
  s_faces_label[sizeof(s_faces_label) - 1] = '\0';

  s_faces_mounted = mount_littlefs(s_faces_fs, s_faces_label, "/faces");
  s_assets_mounted = mount_littlefs(s_assets_fs, "assets", "/assets");

  if (!s_common_mounted) {
    s_common_mounted = FFat.begin(false, "/commonplace", 5, "commonplace");
    if (!s_common_mounted) {
      s_common_mounted = FFat.begin(true, "/commonplace", 5, "commonplace");
    }
  }
  if (!s_ephem_mounted) {
    s_ephem_mounted = FFat.begin(false, "/ephemeris", 5, "ephemeris");
    if (!s_ephem_mounted) {
      s_ephem_mounted = FFat.begin(true, "/ephemeris", 5, "ephemeris");
    }
  }
  if (!s_crash_mounted) {
    s_crash_mounted = FFat.begin(false, "/crashlog", 5, "crashlog");
    if (!s_crash_mounted) {
      s_crash_mounted = FFat.begin(true, "/crashlog", 5, "crashlog");
    }
  }
  return s_faces_mounted;
}

bool pm_partitions_faces_mounted(void) { return s_faces_mounted; }
bool pm_partitions_assets_mounted(void) { return s_assets_mounted; }

fs::FS &pm_partitions_faces_fs(void) { return s_faces_fs; }

const char *pm_partitions_faces_mount_path(void) { return "/faces"; }

bool pm_partitions_mount_faces_label(const char *label) {
  if (!label || !label[0]) {
    return false;
  }
  s_faces_fs.end();
  s_faces_mounted = mount_littlefs(s_faces_fs, label, "/faces");
  if (s_faces_mounted) {
    strncpy(s_faces_label, label, sizeof(s_faces_label) - 1);
    s_faces_label[sizeof(s_faces_label) - 1] = '\0';
  }
  return s_faces_mounted;
}

bool pm_partitions_erase_faces_label(const char *label) {
  const esp_partition_t *part =
      esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, label);
  if (!part) {
    return false;
  }
  return esp_partition_erase_range(part, 0, part->size) == ESP_OK;
}
