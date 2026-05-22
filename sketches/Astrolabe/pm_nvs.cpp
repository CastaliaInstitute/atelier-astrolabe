#include "pm_nvs.h"

#include <nvs.h>
#include <string.h>

namespace {

bool open_ns(const char *ns, nvs_open_mode_t mode, nvs_handle_t *out) {
  return ns && ns[0] && out && nvs_open(ns, mode, out) == ESP_OK;
}

bool commit_and_close(nvs_handle_t h, esp_err_t err) {
  if (err == ESP_OK) {
    err = nvs_commit(h);
  }
  nvs_close(h);
  return err == ESP_OK;
}

void copy_default(char *out, size_t cap, const char *def) {
  if (!out || cap == 0) {
    return;
  }
  if (!def) {
    def = "";
  }
  strncpy(out, def, cap - 1);
  out[cap - 1] = '\0';
}

}  // namespace

bool pm_nvs_has_key(const char *ns, const char *key) {
  nvs_handle_t h = 0;
  if (!key || !open_ns(ns, NVS_READONLY, &h)) {
    return false;
  }
  uint8_t u8 = 0;
  uint16_t u16 = 0;
  uint32_t u32 = 0;
  int32_t i32 = 0;
  size_t len = 0;
  const bool ok = nvs_get_u8(h, key, &u8) == ESP_OK || nvs_get_u16(h, key, &u16) == ESP_OK ||
                  nvs_get_u32(h, key, &u32) == ESP_OK || nvs_get_i32(h, key, &i32) == ESP_OK ||
                  nvs_get_str(h, key, nullptr, &len) == ESP_OK || nvs_get_blob(h, key, nullptr, &len) == ESP_OK;
  nvs_close(h);
  return ok;
}

bool pm_nvs_remove(const char *ns, const char *key) {
  nvs_handle_t h = 0;
  if (!key || !open_ns(ns, NVS_READWRITE, &h)) {
    return false;
  }
  const esp_err_t err = nvs_erase_key(h, key);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    nvs_close(h);
    return true;
  }
  return commit_and_close(h, err);
}

uint8_t pm_nvs_get_u8(const char *ns, const char *key, uint8_t def) {
  nvs_handle_t h = 0;
  uint8_t value = def;
  if (key && open_ns(ns, NVS_READONLY, &h)) {
    (void)nvs_get_u8(h, key, &value);
    nvs_close(h);
  }
  return value;
}

uint16_t pm_nvs_get_u16(const char *ns, const char *key, uint16_t def) {
  nvs_handle_t h = 0;
  uint16_t value = def;
  if (key && open_ns(ns, NVS_READONLY, &h)) {
    (void)nvs_get_u16(h, key, &value);
    nvs_close(h);
  }
  return value;
}

uint32_t pm_nvs_get_u32(const char *ns, const char *key, uint32_t def) {
  nvs_handle_t h = 0;
  uint32_t value = def;
  if (key && open_ns(ns, NVS_READONLY, &h)) {
    (void)nvs_get_u32(h, key, &value);
    nvs_close(h);
  }
  return value;
}

int32_t pm_nvs_get_i32(const char *ns, const char *key, int32_t def) {
  nvs_handle_t h = 0;
  int32_t value = def;
  if (key && open_ns(ns, NVS_READONLY, &h)) {
    (void)nvs_get_i32(h, key, &value);
    nvs_close(h);
  }
  return value;
}

bool pm_nvs_get_bool(const char *ns, const char *key, bool def) {
  return pm_nvs_get_u8(ns, key, def ? 1u : 0u) == 1u;
}

float pm_nvs_get_float(const char *ns, const char *key, float def) {
  nvs_handle_t h = 0;
  float value = def;
  size_t len = sizeof(value);
  if (key && open_ns(ns, NVS_READONLY, &h)) {
    (void)nvs_get_blob(h, key, &value, &len);
    nvs_close(h);
  }
  return value;
}

size_t pm_nvs_get_str(const char *ns, const char *key, char *out, size_t cap, const char *def) {
  if (!out || cap == 0) {
    return 0;
  }
  copy_default(out, cap, def);
  nvs_handle_t h = 0;
  if (!key || !open_ns(ns, NVS_READONLY, &h)) {
    return strlen(out);
  }
  size_t len = 0;
  esp_err_t err = nvs_get_str(h, key, nullptr, &len);
  if (err == ESP_OK && len <= cap) {
    err = nvs_get_str(h, key, out, &len);
  }
  nvs_close(h);
  return err == ESP_OK ? len : strlen(out);
}

bool pm_nvs_set_u8(const char *ns, const char *key, uint8_t value) {
  nvs_handle_t h = 0;
  if (!key || !open_ns(ns, NVS_READWRITE, &h)) {
    return false;
  }
  return commit_and_close(h, nvs_set_u8(h, key, value));
}

bool pm_nvs_set_u16(const char *ns, const char *key, uint16_t value) {
  nvs_handle_t h = 0;
  if (!key || !open_ns(ns, NVS_READWRITE, &h)) {
    return false;
  }
  return commit_and_close(h, nvs_set_u16(h, key, value));
}

bool pm_nvs_set_u32(const char *ns, const char *key, uint32_t value) {
  nvs_handle_t h = 0;
  if (!key || !open_ns(ns, NVS_READWRITE, &h)) {
    return false;
  }
  return commit_and_close(h, nvs_set_u32(h, key, value));
}

bool pm_nvs_set_i32(const char *ns, const char *key, int32_t value) {
  nvs_handle_t h = 0;
  if (!key || !open_ns(ns, NVS_READWRITE, &h)) {
    return false;
  }
  return commit_and_close(h, nvs_set_i32(h, key, value));
}

bool pm_nvs_set_bool(const char *ns, const char *key, bool value) {
  return pm_nvs_set_u8(ns, key, value ? 1u : 0u);
}

bool pm_nvs_set_float(const char *ns, const char *key, float value) {
  nvs_handle_t h = 0;
  if (!key || !open_ns(ns, NVS_READWRITE, &h)) {
    return false;
  }
  return commit_and_close(h, nvs_set_blob(h, key, &value, sizeof(value)));
}

bool pm_nvs_set_str(const char *ns, const char *key, const char *value) {
  nvs_handle_t h = 0;
  if (!key || !value || !open_ns(ns, NVS_READWRITE, &h)) {
    return false;
  }
  return commit_and_close(h, nvs_set_str(h, key, value));
}
