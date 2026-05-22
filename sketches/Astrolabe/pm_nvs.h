#pragma once

#include <stddef.h>
#include <stdint.h>

bool pm_nvs_has_key(const char *ns, const char *key);
bool pm_nvs_remove(const char *ns, const char *key);

bool pm_nvs_get_bool(const char *ns, const char *key, bool def);
uint8_t pm_nvs_get_u8(const char *ns, const char *key, uint8_t def);
uint16_t pm_nvs_get_u16(const char *ns, const char *key, uint16_t def);
uint32_t pm_nvs_get_u32(const char *ns, const char *key, uint32_t def);
int32_t pm_nvs_get_i32(const char *ns, const char *key, int32_t def);
float pm_nvs_get_float(const char *ns, const char *key, float def);
size_t pm_nvs_get_str(const char *ns, const char *key, char *out, size_t cap, const char *def = "");

bool pm_nvs_set_bool(const char *ns, const char *key, bool value);
bool pm_nvs_set_u8(const char *ns, const char *key, uint8_t value);
bool pm_nvs_set_u16(const char *ns, const char *key, uint16_t value);
bool pm_nvs_set_u32(const char *ns, const char *key, uint32_t value);
bool pm_nvs_set_i32(const char *ns, const char *key, int32_t value);
bool pm_nvs_set_float(const char *ns, const char *key, float value);
bool pm_nvs_set_str(const char *ns, const char *key, const char *value);
