#pragma once

class WiFiClientSecure;

/** Configure WiFiClientSecure with the pinned CA bundle used by Castalia HTTPS. */
void pm_tls_configure_client(WiFiClientSecure &client);

const char *pm_tls_ca_bundle_name();
