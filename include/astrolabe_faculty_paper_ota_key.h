#pragma once

/*
 * OTA manifest verification key.
 *
 * Development key shared with the other faculty updater bring-up targets.
 * Replace with the production public key before promoting updater firmware.
 */
#define ASTROLABE_FACULTY_PAPER_OTA_PUBKEY_PEM                 \
    "-----BEGIN PUBLIC KEY-----\n"                             \
    "MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEveHXaVQsNDFQIAtSXR65eds4GndZ\n" \
    "I/RI+Z3xXpt4gs7crf4evvPJC4VK+TTA4V05OtjANxGtqZbrf1f5twiT+g==\n" \
    "-----END PUBLIC KEY-----\n"

#define ASTROLABE_FACULTY_PAPER_OTA_SIG_ALG "ecdsa-p256-sha256"
