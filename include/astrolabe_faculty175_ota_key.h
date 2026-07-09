#pragma once

/*
 * OTA manifest verification key.
 *
 * This is a development key for the 1.75C recovery updater bring-up. Replace
 * it with the production public key before promoting updater firmware.
 */
#define ASTROLABE_FACULTY175_OTA_PUBKEY_PEM                    \
    "-----BEGIN PUBLIC KEY-----\n"                             \
    "MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAE/UfEPMZODpYfDRObEwUqQ0LLsIiO\n" \
    "T/Vq4D8tlmuzQHwqYUvD1HnesAiBSdQaPqZ5h134ameXIHt3iB05QTXC5w==\n" \
    "-----END PUBLIC KEY-----\n"

#define ASTROLABE_FACULTY175_OTA_SIG_ALG "ecdsa-p256-sha256"
