#include "pm_ota_nvs.h"

#include <Arduino.h>
#include <Preferences.h>
#include <string.h>

static Preferences s_pref;
static bool s_open = false;

static void ensure_open() {
  if (!s_open) {
    s_open = s_pref.begin(kPmOtaNvsNs, false);
  }
}

void pm_ota_nvs_begin(void) { ensure_open(); }

static void get_str(const char *key, char *out, size_t out_sz, const char *def) {
  ensure_open();
  if (!s_open || !out || out_sz == 0) {
    if (out && out_sz) {
      strncpy(out, def ? def : "", out_sz - 1);
      out[out_sz - 1] = '\0';
    }
    return;
  }
  const String v = s_pref.getString(key, def ? def : "");
  strncpy(out, v.c_str(), out_sz - 1);
  out[out_sz - 1] = '\0';
}

static void put_str(const char *key, const char *val) {
  ensure_open();
  if (s_open && val) {
    s_pref.putString(key, val);
  }
}

const char *pm_ota_nvs_face_active_partition(void) {
  static char buf[16];
  get_str("face.active", buf, sizeof(buf), kPmFacePartA);
  return buf;
}

void pm_ota_nvs_face_set_active_partition(const char *label) { put_str("face.active", label); }

const char *pm_ota_nvs_face_previous_partition(void) {
  static char buf[16];
  get_str("face.prev", buf, sizeof(buf), kPmFacePartB);
  return buf;
}

void pm_ota_nvs_face_set_previous_partition(const char *label) { put_str("face.prev", label); }

const char *pm_ota_nvs_face_active_version(void) {
  static char buf[24];
  get_str("face.ver", buf, sizeof(buf), "0.0.0");
  return buf;
}

void pm_ota_nvs_face_set_active_version(const char *ver) { put_str("face.ver", ver); }

const char *pm_ota_nvs_face_pending_version(void) {
  static char buf[24];
  get_str("face.pver", buf, sizeof(buf), "");
  return buf;
}

void pm_ota_nvs_face_set_pending_version(const char *ver) { put_str("face.pver", ver); }

bool pm_ota_nvs_face_pending(void) {
  ensure_open();
  return s_open ? s_pref.getBool("face.pending", false) : false;
}

void pm_ota_nvs_face_set_pending(bool pending) {
  ensure_open();
  if (s_open) {
    s_pref.putBool("face.pending", pending);
  }
}

uint8_t pm_ota_nvs_face_boot_attempts(void) {
  ensure_open();
  return s_open ? s_pref.getUChar("face.boot_try", 0) : 0;
}

void pm_ota_nvs_face_set_boot_attempts(uint8_t n) {
  ensure_open();
  if (s_open) {
    s_pref.putUChar("face.boot_try", n);
  }
}

const char *pm_ota_nvs_face_last_good_id(void) {
  static char buf[32];
  get_str("face.last_ok", buf, sizeof(buf), "face.classic_analog");
  return buf;
}

void pm_ota_nvs_face_set_last_good_id(const char *id) { put_str("face.last_ok", id); }

PmUpdateChannel pm_ota_nvs_update_channel(void) {
  ensure_open();
  const uint8_t v = s_open ? s_pref.getUChar("upd.ch", 0) : 0;
  if (v > static_cast<uint8_t>(PmUpdateChannel::Factory)) {
    return PmUpdateChannel::Stable;
  }
  return static_cast<PmUpdateChannel>(v);
}

void pm_ota_nvs_set_update_channel(PmUpdateChannel ch) {
  ensure_open();
  if (s_open) {
    s_pref.putUChar("upd.ch", static_cast<uint8_t>(ch));
  }
}

bool pm_ota_nvs_allow_downgrade(void) {
  ensure_open();
  return s_open ? s_pref.getBool("upd.downgrade", false) : false;
}

void pm_ota_nvs_set_allow_downgrade(bool allow) {
  ensure_open();
  if (s_open) {
    s_pref.putBool("upd.downgrade", allow);
  }
}

int64_t pm_ota_nvs_last_check_epoch(void) {
  ensure_open();
  return s_open ? s_pref.getLong64("upd.last_chk", 0) : 0;
}

void pm_ota_nvs_set_last_check_epoch(int64_t epoch) {
  ensure_open();
  if (s_open) {
    s_pref.putLong64("upd.last_chk", epoch);
  }
}

const char *pm_ota_nvs_inactive_face_partition(void) {
  const char *active = pm_ota_nvs_face_active_partition();
  if (strcmp(active, kPmFacePartA) == 0) {
    return kPmFacePartB;
  }
  return kPmFacePartA;
}
