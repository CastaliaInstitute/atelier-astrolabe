#pragma once

class WebServer;

/** Register GET/POST handlers for the on-device LAN settings UI. */
void pm_settings_http_register(WebServer *server);
