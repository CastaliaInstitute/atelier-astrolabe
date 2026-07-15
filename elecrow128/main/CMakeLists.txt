idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES
        app_update
        driver
        esp_app_format
        esp_event
        esp_lcd
        esp_netif
        esp_system
        esp_wifi
        esp_driver_spi
        nvs_flash
)
