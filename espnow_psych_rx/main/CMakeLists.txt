idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp_event
        esp_netif
        esp_system
        esp_timer
        esp_wifi
        nvs_flash
)
