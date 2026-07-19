#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

/* Registers the Astrolabe-served launcher for Codex Desktop Remote pairing.
 * The launcher is available in the Cyber build and deliberately hands the
 * scanned link to the official ChatGPT client instead of interpreting private
 * Remote credentials on the device. */
esp_err_t faculty175_codex_remote_http_register(httpd_handle_t server);
