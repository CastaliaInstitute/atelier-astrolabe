#include "faculty175_device_auth.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "mbedtls/md.h"
#include "nvs.h"

#include "astrolabe_faculty175_face.h"

static const char *TAG = "faculty175_device_auth";

#define DEV_AUTH_NVS_NS "device_auth"
#define DEV_AUTH_NVS_SECRET "secret"
#define DEV_AUTH_SECRET_BYTES 32
#define DEV_AUTH_SECRET_HEX_LEN (DEV_AUTH_SECRET_BYTES * 2)
#define DEV_AUTH_NONCE_BYTES 16
#define DEV_AUTH_SIG_HEX_LEN 64

static char s_mac[18];
static char s_secret_hex[DEV_AUTH_SECRET_HEX_LEN + 1];
static char s_console_nonce[DEV_AUTH_NONCE_BYTES * 2 + 1];

static void bytes_to_hex(const uint8_t *bytes, size_t len, char *out, size_t cap)
{
    static const char alphabet[] = "0123456789abcdef";
    if (out == NULL || cap < len * 2 + 1) {
        return;
    }
    for (size_t i = 0; i < len; ++i) {
        out[i * 2] = alphabet[(bytes[i] >> 4) & 0x0f];
        out[i * 2 + 1] = alphabet[bytes[i] & 0x0f];
    }
    out[len * 2] = '\0';
}

static bool hex_to_bytes(const char *hex, uint8_t *out, size_t out_len)
{
    if (hex == NULL || out == NULL || strlen(hex) != out_len * 2) {
        return false;
    }
    for (size_t i = 0; i < out_len; ++i) {
        char pair[3] = { hex[i * 2], hex[i * 2 + 1], '\0' };
        if (!isxdigit((unsigned char)pair[0]) || !isxdigit((unsigned char)pair[1])) {
            return false;
        }
        char *end = NULL;
        unsigned long v = strtoul(pair, &end, 16);
        if (end == NULL || *end != '\0' || v > 255) {
            return false;
        }
        out[i] = (uint8_t)v;
    }
    return true;
}

esp_err_t faculty175_device_auth_mac(char *out, size_t cap)
{
    if (out == NULL || cap < sizeof(s_mac)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_mac[0] == '\0') {
        uint8_t mac[6];
        esp_err_t err = esp_efuse_mac_get_default(mac);
        if (err != ESP_OK) {
            return err;
        }
        snprintf(s_mac,
                 sizeof(s_mac),
                 "%02x:%02x:%02x:%02x:%02x:%02x",
                 mac[0],
                 mac[1],
                 mac[2],
                 mac[3],
                 mac[4],
                 mac[5]);
    }
    strlcpy(out, s_mac, cap);
    return ESP_OK;
}

static esp_err_t generate_secret(char *out, size_t cap)
{
    if (out == NULL || cap < DEV_AUTH_SECRET_HEX_LEN + 1) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t secret[DEV_AUTH_SECRET_BYTES];
    esp_fill_random(secret, sizeof(secret));
    bytes_to_hex(secret, sizeof(secret), out, cap);
    return ESP_OK;
}

esp_err_t faculty175_device_auth_init(void)
{
    char mac[18];
    esp_err_t err = faculty175_device_auth_mac(mac, sizeof(mac));
    if (err != ESP_OK) {
        return err;
    }

    nvs_handle_t nvs;
    err = nvs_open(DEV_AUTH_NVS_NS, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }

    size_t len = sizeof(s_secret_hex);
    err = nvs_get_str(nvs, DEV_AUTH_NVS_SECRET, s_secret_hex, &len);
    if (err == ESP_ERR_NVS_NOT_FOUND || strlen(s_secret_hex) != DEV_AUTH_SECRET_HEX_LEN) {
        err = generate_secret(s_secret_hex, sizeof(s_secret_hex));
        if (err == ESP_OK) {
            err = nvs_set_str(nvs, DEV_AUTH_NVS_SECRET, s_secret_hex);
        }
        if (err == ESP_OK) {
            err = nvs_commit(nvs);
        }
        if (err == ESP_OK) {
            ESP_LOGW(TAG, "generated device credential mac=%s; provision with: device provision", s_mac);
        }
    }
    nvs_close(nvs);
    return err;
}

static esp_err_t nonce_hex(char *out, size_t cap)
{
    if (out == NULL || cap < DEV_AUTH_NONCE_BYTES * 2 + 1) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t nonce[DEV_AUTH_NONCE_BYTES];
    esp_fill_random(nonce, sizeof(nonce));
    bytes_to_hex(nonce, sizeof(nonce), out, cap);
    return ESP_OK;
}

static esp_err_t signature_hex(const char *nonce, char *out, size_t cap)
{
    if (nonce == NULL || out == NULL || cap < DEV_AUTH_SIG_HEX_LEN + 1) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t secret[DEV_AUTH_SECRET_BYTES];
    if (!hex_to_bytes(s_secret_hex, secret, sizeof(secret))) {
        return ESP_ERR_INVALID_STATE;
    }
    char payload[160];
    const int n = snprintf(payload,
                           sizeof(payload),
                           "%s\n%s\n%s\n",
                           s_mac,
                           nonce,
                           ASTROLABE_FACULTY_OTA_CHANNEL);
    if (n <= 0 || (size_t)n >= sizeof(payload)) {
        return ESP_ERR_INVALID_SIZE;
    }
    uint8_t digest[32];
    int rc = mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),
                             secret,
                             sizeof(secret),
                             (const unsigned char *)payload,
                             strlen(payload),
                             digest);
    if (rc != 0) {
        return ESP_FAIL;
    }
    bytes_to_hex(digest, sizeof(digest), out, cap);
    return ESP_OK;
}

esp_err_t faculty175_device_auth_headers(esp_http_client_handle_t client)
{
    if (client == NULL || s_secret_hex[0] == '\0') {
        return ESP_ERR_INVALID_STATE;
    }
    char nonce[DEV_AUTH_NONCE_BYTES * 2 + 1];
    char sig[DEV_AUTH_SIG_HEX_LEN + 1];
    esp_err_t err = nonce_hex(nonce, sizeof(nonce));
    if (err == ESP_OK) {
        err = signature_hex(nonce, sig, sizeof(sig));
    }
    if (err != ESP_OK) {
        return err;
    }
    esp_http_client_set_header(client, "X-Astrolabe-Device-Mac", s_mac);
    esp_http_client_set_header(client, "X-Astrolabe-Device-Nonce", nonce);
    esp_http_client_set_header(client, "X-Astrolabe-Device-Signature", sig);
    esp_http_client_set_header(client, "X-Astrolabe-Device-Channel", ASTROLABE_FACULTY_OTA_CHANNEL);
    return ESP_OK;
}

esp_err_t faculty175_device_auth_header_text(char *out, size_t cap)
{
    if (out == NULL || cap == 0 || s_secret_hex[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    char nonce[DEV_AUTH_NONCE_BYTES * 2 + 1];
    char sig[DEV_AUTH_SIG_HEX_LEN + 1];
    esp_err_t err = nonce_hex(nonce, sizeof(nonce));
    if (err == ESP_OK) {
        err = signature_hex(nonce, sig, sizeof(sig));
    }
    if (err != ESP_OK) {
        return err;
    }
    const int written = snprintf(out,
                                 cap,
                                 "X-Astrolabe-Device-Mac: %s\r\n"
                                 "X-Astrolabe-Device-Nonce: %s\r\n"
                                 "X-Astrolabe-Device-Signature: %s\r\n"
                                 "X-Astrolabe-Device-Channel: %s\r\n",
                                 s_mac,
                                 nonce,
                                 sig,
                                 ASTROLABE_FACULTY_OTA_CHANNEL);
    return written > 0 && (size_t)written < cap ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

bool faculty175_device_auth_handle(const char *line)
{
    if (line == NULL || (strcasecmp(line, "device") != 0 && strncasecmp(line, "device ", 7) != 0)) {
        return false;
    }
    const char *sub = line + 6;
    while (*sub == ' ') {
        ++sub;
    }
    if (*sub == '\0' || strcasecmp(sub, "help") == 0) {
        printf("device commands:\n");
        printf("  device status\n");
        printf("  device provision\n");
        printf("  device rotate <64-hex-secret>\n");
        fflush(stdout);
        return true;
    }
    if (strcasecmp(sub, "status") == 0) {
        printf("device: mac=%s secret=%s channel=%s\n",
               s_mac,
               s_secret_hex[0] ? "present" : "missing",
               ASTROLABE_FACULTY_OTA_CHANNEL);
        fflush(stdout);
        return true;
    }
    if (strcasecmp(sub, "provision") == 0) {
        printf("device: provision mac=%s secret=%s channel=%s\n",
               s_mac,
               s_secret_hex,
               ASTROLABE_FACULTY_OTA_CHANNEL);
        fflush(stdout);
        return true;
    }
    if (strncasecmp(sub, "rotate ", 7) == 0) {
        const char *candidate = sub + 7;
        while (*candidate == ' ') {
            ++candidate;
        }
        uint8_t decoded[DEV_AUTH_SECRET_BYTES];
        if (strlen(candidate) != DEV_AUTH_SECRET_HEX_LEN ||
            !hex_to_bytes(candidate, decoded, sizeof(decoded))) {
            printf("device: rotate ESP_ERR_INVALID_ARG\n");
            fflush(stdout);
            return true;
        }
        nvs_handle_t nvs = 0;
        esp_err_t err = nvs_open(DEV_AUTH_NVS_NS, NVS_READWRITE, &nvs);
        if (err == ESP_OK) {
            err = nvs_set_str(nvs, DEV_AUTH_NVS_SECRET, candidate);
        }
        if (err == ESP_OK) {
            err = nvs_commit(nvs);
        }
        if (nvs != 0) {
            nvs_close(nvs);
        }
        if (err == ESP_OK) {
            strlcpy(s_secret_hex, candidate, sizeof(s_secret_hex));
        }
        printf("device: rotate %s\n", esp_err_to_name(err));
        fflush(stdout);
        return true;
    }
    printf("device: unknown subcommand \"%s\" (try: device help)\n", sub);
    fflush(stdout);
    return true;
}

esp_err_t faculty175_device_auth_console_challenge(char *out_nonce, size_t cap)
{
    esp_err_t err = nonce_hex(s_console_nonce, sizeof(s_console_nonce));
    if (err != ESP_OK || out_nonce == NULL || cap < sizeof(s_console_nonce)) {
        return err != ESP_OK ? err : ESP_ERR_INVALID_SIZE;
    }
    strlcpy(out_nonce, s_console_nonce, cap);
    return ESP_OK;
}

bool faculty175_device_auth_console_verify(const char *nonce, const char *signature)
{
    char expected[DEV_AUTH_SIG_HEX_LEN + 1] = {};
    if (nonce == NULL || signature == NULL || s_console_nonce[0] == '\0' ||
        strcmp(nonce, s_console_nonce) != 0 || signature_hex(nonce, expected, sizeof(expected)) != ESP_OK) {
        return false;
    }
    const size_t len = strlen(expected);
    unsigned char mismatch = (unsigned char)(strlen(signature) != len);
    for (size_t i = 0; i < len; ++i) {
        mismatch |= (unsigned char)(expected[i] ^ signature[i]);
    }
    s_console_nonce[0] = '\0'; /* one command per challenge prevents replay */
    return mismatch == 0;
}
