#pragma once

class WebServer;

/** Register GET/POST handlers for on-device settings UI. */
void pm_settings_http_register(WebServer *server);
