idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES bt nvs_flash driver esp_driver_gpio esp_driver_i2c esp_driver_spi esp_event esp_netif esp_timer esp_wifi)
