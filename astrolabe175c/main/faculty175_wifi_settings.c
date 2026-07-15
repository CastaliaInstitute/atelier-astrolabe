#include "faculty175_wifi_settings.h"

#include <stdio.h>
#include <string.h>

#include "nvs_flash.h"

#include "faculty175_util.h"

static const char *kNs = "wifi_cfg";
static const char *kSsid = "ssid";
static const char *kPass = "pass";
static const char *kCount = "count";
static const char *kTravelRouter = "router";

static bool s_ap_active;
static unsigned s_ap_client_count;
static bool s_scan_suppressed;
static char s_ssid[FACULTY175_WIFI_SSID_MAX + 1];
static char s_upstream_ssid[FACULTY175_WIFI_SSID_MAX + 1];
static char s_pass[FACULTY175_WIFI_PASS_MAX + 1];
static char s_url[FACULTY175_WIFI_URL_MAX];
static char s_ap_qr[FACULTY175_WIFI_QR_MAX];
static char s_page_qr[FACULTY175_WIFI_URL_MAX];
static char s_status[48] = "WiFi not started";

static void set_url(const esp_ip4_addr_t *ip)
{
    if (ip != NULL && ip->addr != 0) {
        snprintf(s_url, sizeof(s_url), "http://" IPSTR "/wifi", IP2STR(ip));
    } else {
        s_url[0] = '\0';
    }
}

static void set_ap_qr(void)
{
    if (s_pass[0] != '\0') {
        snprintf(s_ap_qr, sizeof(s_ap_qr), "WIFI:T:WPA;S:%s;P:%s;;", s_ssid, s_pass);
    } else {
        snprintf(s_ap_qr, sizeof(s_ap_qr), "WIFI:T:nopass;S:%s;;", s_ssid);
    }
}

static void set_page_qr(void)
{
    if (s_url[0] != '\0') {
        faculty175_strlcpy(s_page_qr, s_url, sizeof(s_page_qr));
    } else if (s_ap_active) {
        faculty175_strlcpy(s_page_qr, "http://192.168.4.1/wifi", sizeof(s_page_qr));
    } else {
        s_page_qr[0] = '\0';
    }
}

static void known_key(char *out, size_t cap, const char *prefix, size_t index)
{
    snprintf(out, cap, "%s%u", prefix, (unsigned)index);
}

static esp_err_t load_legacy(nvs_handle_t nvs, faculty175_wifi_known_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    size_t len = sizeof(out->ssid);
    esp_err_t err = nvs_get_str(nvs, kSsid, out->ssid, &len);
    if (err != ESP_OK || out->ssid[0] == '\0') {
        return err;
    }
    len = sizeof(out->pass);
    esp_err_t pass_err = nvs_get_str(nvs, kPass, out->pass, &len);
    if (pass_err != ESP_OK) {
        out->pass[0] = '\0';
    }
    return ESP_OK;
}

esp_err_t faculty175_wifi_settings_load(char *ssid, size_t ssid_cap, char *pass, size_t pass_cap)
{
    if (ssid == NULL || ssid_cap == 0 || pass == NULL || pass_cap == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    ssid[0] = '\0';
    pass[0] = '\0';

    nvs_handle_t nvs;
    esp_err_t err = nvs_open(kNs, NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    faculty175_wifi_known_t known = {};
    faculty175_wifi_known_t known_list[FACULTY175_WIFI_KNOWN_MAX] = {};
    const size_t count = faculty175_wifi_settings_load_known(known_list, FACULTY175_WIFI_KNOWN_MAX);
    if (count > 0) {
        known = known_list[0];
        err = ESP_OK;
    } else {
        err = load_legacy(nvs, &known);
    }
    if (err == ESP_OK && known.ssid[0] != '\0') {
        faculty175_strlcpy(ssid, known.ssid, ssid_cap);
        faculty175_strlcpy(pass, known.pass, pass_cap);
    }
    nvs_close(nvs);
    return err;
}

esp_err_t faculty175_wifi_settings_save(const char *ssid, const char *pass)
{
    return faculty175_wifi_settings_add_known(ssid, pass, true);
}

size_t faculty175_wifi_settings_load_known(faculty175_wifi_known_t *out, size_t cap)
{
    if (out == NULL || cap == 0) {
        return 0;
    }
    memset(out, 0, sizeof(*out) * cap);

    nvs_handle_t nvs;
    esp_err_t err = nvs_open(kNs, NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        return 0;
    }

    uint8_t stored_count = 0;
    (void)nvs_get_u8(nvs, kCount, &stored_count);
    size_t count = 0;
    const size_t limit = stored_count > FACULTY175_WIFI_KNOWN_MAX ? FACULTY175_WIFI_KNOWN_MAX : stored_count;
    for (size_t i = 0; i < limit && count < cap; ++i) {
        char ssid_key[12] = {};
        char pass_key[12] = {};
        known_key(ssid_key, sizeof(ssid_key), "ssid", i);
        known_key(pass_key, sizeof(pass_key), "pass", i);
        size_t len = sizeof(out[count].ssid);
        if (nvs_get_str(nvs, ssid_key, out[count].ssid, &len) != ESP_OK || out[count].ssid[0] == '\0') {
            continue;
        }
        len = sizeof(out[count].pass);
        if (nvs_get_str(nvs, pass_key, out[count].pass, &len) != ESP_OK) {
            out[count].pass[0] = '\0';
        }
        ++count;
    }

    if (count == 0 && cap > 0) {
        faculty175_wifi_known_t legacy = {};
        if (load_legacy(nvs, &legacy) == ESP_OK && legacy.ssid[0] != '\0') {
            out[0] = legacy;
            count = 1;
        }
    }
    nvs_close(nvs);
    return count;
}

static esp_err_t save_known_list(const faculty175_wifi_known_t *known, size_t count)
{
    if (known == NULL && count > 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (count > FACULTY175_WIFI_KNOWN_MAX) {
        count = FACULTY175_WIFI_KNOWN_MAX;
    }
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(kNs, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_u8(nvs, kCount, (uint8_t)count);
    for (size_t i = 0; err == ESP_OK && i < FACULTY175_WIFI_KNOWN_MAX; ++i) {
        char ssid_key[12] = {};
        char pass_key[12] = {};
        known_key(ssid_key, sizeof(ssid_key), "ssid", i);
        known_key(pass_key, sizeof(pass_key), "pass", i);
        if (i < count && known[i].ssid[0] != '\0') {
            err = nvs_set_str(nvs, ssid_key, known[i].ssid);
            if (err == ESP_OK) {
                err = nvs_set_str(nvs, pass_key, known[i].pass);
            }
        } else {
            (void)nvs_erase_key(nvs, ssid_key);
            (void)nvs_erase_key(nvs, pass_key);
        }
    }
    if (err == ESP_OK && count > 0) {
        err = nvs_set_str(nvs, kSsid, known[0].ssid);
        if (err == ESP_OK) {
            err = nvs_set_str(nvs, kPass, known[0].pass);
        }
    } else if (err == ESP_OK) {
        (void)nvs_erase_key(nvs, kSsid);
        (void)nvs_erase_key(nvs, kPass);
    }
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return err;
}

esp_err_t faculty175_wifi_settings_add_known(const char *ssid, const char *pass, bool make_primary)
{
    if (ssid == NULL || ssid[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    faculty175_wifi_known_t known[FACULTY175_WIFI_KNOWN_MAX] = {};
    size_t count = faculty175_wifi_settings_load_known(known, FACULTY175_WIFI_KNOWN_MAX);
    faculty175_wifi_known_t entry = {};
    faculty175_strlcpy(entry.ssid, ssid, sizeof(entry.ssid));
    faculty175_strlcpy(entry.pass, pass != NULL ? pass : "", sizeof(entry.pass));

    size_t found = FACULTY175_WIFI_KNOWN_MAX;
    for (size_t i = 0; i < count; ++i) {
        if (strcmp(known[i].ssid, entry.ssid) == 0) {
            found = i;
            break;
        }
    }
    if (found < count) {
        known[found] = entry;
    } else if (count < FACULTY175_WIFI_KNOWN_MAX) {
        found = count++;
        known[found] = entry;
    } else {
        found = FACULTY175_WIFI_KNOWN_MAX - 1;
        known[found] = entry;
    }
    if (make_primary && found > 0 && found < count) {
        faculty175_wifi_known_t primary = known[found];
        memmove(&known[1], &known[0], sizeof(known[0]) * found);
        known[0] = primary;
    }
    return save_known_list(known, count);
}

esp_err_t faculty175_wifi_settings_remove_known(const char *ssid)
{
    if (ssid == NULL || ssid[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    faculty175_wifi_known_t known[FACULTY175_WIFI_KNOWN_MAX] = {};
    size_t count = faculty175_wifi_settings_load_known(known, FACULTY175_WIFI_KNOWN_MAX);
    size_t out = 0;
    for (size_t i = 0; i < count; ++i) {
        if (strcmp(known[i].ssid, ssid) != 0) {
            if (out != i) {
                known[out] = known[i];
            }
            ++out;
        }
    }
    if (out == count) {
        return ESP_ERR_NOT_FOUND;
    }
    return save_known_list(known, out);
}

esp_err_t faculty175_wifi_settings_clear_known(void)
{
    return save_known_list(NULL, 0);
}

bool faculty175_wifi_settings_travel_router_enabled(void)
{
    nvs_handle_t nvs;
    uint8_t enabled = 0;
    if (nvs_open(kNs, NVS_READONLY, &nvs) != ESP_OK) {
        return false;
    }
    (void)nvs_get_u8(nvs, kTravelRouter, &enabled);
    nvs_close(nvs);
    return enabled != 0;
}

esp_err_t faculty175_wifi_settings_set_travel_router_enabled(bool enabled)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(kNs, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_u8(nvs, kTravelRouter, enabled ? 1 : 0);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return err;
}

void faculty175_wifi_settings_set_sta(const char *ssid, const esp_ip4_addr_t *ip)
{
    s_ap_active = false;
    s_ap_client_count = 0;
    faculty175_strlcpy(s_ssid, ssid != NULL ? ssid : "", sizeof(s_ssid));
    faculty175_strlcpy(s_upstream_ssid, s_ssid, sizeof(s_upstream_ssid));
    s_pass[0] = '\0';
    set_url(ip);
    s_ap_qr[0] = '\0';
    set_page_qr();
    snprintf(s_status, sizeof(s_status), "Joined %s", s_ssid[0] != '\0' ? s_ssid : "WiFi");
}

void faculty175_wifi_settings_set_ap(const char *ssid, const char *pass, const esp_ip4_addr_t *ip)
{
    s_ap_active = true;
    s_ap_client_count = 0;
    faculty175_strlcpy(s_ssid, ssid != NULL ? ssid : "", sizeof(s_ssid));
    faculty175_strlcpy(s_pass, pass != NULL ? pass : "", sizeof(s_pass));
    s_upstream_ssid[0] = '\0';
    set_url(ip);
    set_ap_qr();
    set_page_qr();
    snprintf(s_status, sizeof(s_status), "Setup AP %s", s_ssid);
}

void faculty175_wifi_settings_set_router_upstream(const char *ssid, const esp_ip4_addr_t *ip)
{
    faculty175_strlcpy(s_upstream_ssid, ssid != NULL ? ssid : "", sizeof(s_upstream_ssid));
    if (s_ap_active && s_url[0] == '\0') {
        set_url(ip);
    }
    snprintf(s_status,
             sizeof(s_status),
             "Router via %s",
             s_upstream_ssid[0] != '\0' ? s_upstream_ssid : "WiFi");
}

void faculty175_wifi_settings_set_ap_client_count(unsigned count)
{
    s_ap_client_count = count;
    if (!s_ap_active) {
        return;
    }
    if (s_ap_client_count > 0) {
        snprintf(s_status, sizeof(s_status), "Open settings page");
    } else {
        snprintf(s_status, sizeof(s_status), "Scan to join setup AP");
    }
}

void faculty175_wifi_settings_clear_runtime(void)
{
    s_ap_active = false;
    s_ap_client_count = 0;
    s_scan_suppressed = false;
    s_ssid[0] = '\0';
    s_upstream_ssid[0] = '\0';
    s_pass[0] = '\0';
    s_url[0] = '\0';
    s_ap_qr[0] = '\0';
    s_page_qr[0] = '\0';
    faculty175_strlcpy(s_status, "WiFi not started", sizeof(s_status));
}

void faculty175_wifi_settings_set_scan_suppressed(bool suppressed)
{
    s_scan_suppressed = suppressed;
}

bool faculty175_wifi_settings_ap_active(void)
{
    return s_ap_active;
}

bool faculty175_wifi_settings_ap_client_connected(void)
{
    return s_ap_client_count > 0;
}

bool faculty175_wifi_settings_sta_connected(void)
{
    return !s_ap_active && s_ssid[0] != '\0' && s_url[0] != '\0';
}

bool faculty175_wifi_settings_scan_suppressed(void)
{
    return s_scan_suppressed;
}

const char *faculty175_wifi_settings_ssid(void)
{
    return s_ssid;
}

const char *faculty175_wifi_settings_upstream_ssid(void)
{
    return s_upstream_ssid;
}

const char *faculty175_wifi_settings_url(void)
{
    return s_url;
}

const char *faculty175_wifi_settings_qr_payload(void)
{
    if (s_ap_active && s_ap_client_count == 0 && s_ap_qr[0] != '\0') {
        return s_ap_qr;
    }
    return s_page_qr;
}

const char *faculty175_wifi_settings_ap_qr_payload(void)
{
    return s_ap_qr;
}

const char *faculty175_wifi_settings_page_qr_payload(void)
{
    return s_page_qr;
}

const char *faculty175_wifi_settings_status(void)
{
    return s_status;
}
