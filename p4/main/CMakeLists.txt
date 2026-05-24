idf_component_register(
    SRCS
        "main.c"
        "p4_audio.c"
        "p4_network.c"
        "p4_settings.c"
    INCLUDE_DIRS "." "../../include"
    REQUIRES
        ui
        esp_timer
        nvs_flash
        esp_wifi
        esp_netif
        esp_event
        esp_driver_uart
        esp_driver_gpio
        esp_driver_i2s
        esp_codec_dev)
