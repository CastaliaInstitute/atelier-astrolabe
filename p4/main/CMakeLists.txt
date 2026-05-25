idf_component_register(
    SRCS
        "main.c"
        "p4_audio.c"
        "p4_network.c"
        "p4_screen_http.c"
        "p4_settings.c"
        "p4_voice.c"
    INCLUDE_DIRS "." "../../include" "../../lib/minimp3"
    REQUIRES
        p4_real_ui
        esp_timer
        nvs_flash
        esp_wifi
        esp_netif
        esp_event
        esp_http_client
        esp_http_server
        esp-tls
        mbedtls
        esp_driver_uart
        esp_driver_gpio
        esp_driver_i2s
        esp_codec_dev)

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    ASTROLABE_P4_TARGET=1)
