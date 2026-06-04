set(ASTROLABE_PNGDEC "${CMAKE_CURRENT_LIST_DIR}/../../.pio/libdeps/waveshare_s3_175/PNGdec/src")
if(NOT EXISTS "${ASTROLABE_PNGDEC}/PNGdec.cpp")
    message(FATAL_ERROR "Missing PNGdec. Run pio pkg install for waveshare_s3_175 or set ASTROLABE_PNGDEC.")
endif()

file(GLOB PNGDEC_SRCS "${ASTROLABE_PNGDEC}/*.cpp" "${ASTROLABE_PNGDEC}/*.c")
file(GLOB FACULTY175_FACE_SRCS "${CMAKE_CURRENT_LIST_DIR}/faculty175_face_*.c")

idf_component_register(
    SRCS
        "main.c"
        "faculty175_board.c"
        "faculty175_board_id.c"
        "faculty175_touch.c"
        "faculty175_gesture.c"
        "faculty175_pmu.cpp"
        "faculty175_serial.c"
        "faculty175_almanac.c"
        "faculty175_charts.c"
        "faculty175_device_auth.c"
        ${FACULTY175_FACE_SRCS}
        "faculty175_faces.c"
        "../../lib/astrolabe_display_core/astrolabe_round_bezel.c"
        "faculty175_ota.c"
        "faculty175_qa.c"
        "faculty175_listen.c"
        "faculty175_faculty.c"
        "faculty175_faculty_roster.c"
        "faculty175_faculty_png.cpp"
        ${PNGDEC_SRCS}
    INCLUDE_DIRS "." "../../include" "../../lib/astrolabe_display_core/include" "${ASTROLABE_PNGDEC}"
    REQUIRES
        driver
        esp_event
        esp_http_client
        esp_lcd
        espressif__esp_lcd_touch
        waveshare__esp_lcd_touch_cst9217
        esp_netif
        esp-tls
        esp_wifi
        app_update
        json
        spi_flash
        minimp3
        nvs_flash
        spiffs
        mbedtls
        esp_new_jpeg
        esp_codec_dev
        XPowersLib
        astrolabe_audio_pipeline
        astrolabe_time
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE XPOWERS_CHIP_AXP2101)

target_compile_definitions(${COMPONENT_LIB} PRIVATE __LINUX__ PNG_MAX_BUFFERED_PIXELS=8194)
