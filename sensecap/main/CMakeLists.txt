idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "." "../../include"
    REQUIRES
        driver
        esp_event
        esp_http_client
        esp_netif
        esp_adc
        esp-tls
        esp_hw_support
        esp_io_expander_pca95xx_16bit
        esp_jpeg_simd
        esp_lcd
        esp_lvgl_port
        esp_wifi
        minimp3
        nvs_flash
        sensecap-watcher
        sscma_client
        espressif__button
        espressif__esp_codec_dev
        espressif__esp_io_expander
        espressif__esp_lcd_spd2010
        espressif__esp_lcd_touch_spd2010
        espressif__knob
        espressif__led_strip
)
