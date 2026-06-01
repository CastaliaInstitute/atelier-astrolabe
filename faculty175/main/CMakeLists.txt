set(ASTROLABE_PNGDEC "${CMAKE_CURRENT_LIST_DIR}/../../.pio/libdeps/waveshare_s3_175/PNGdec/src")
if(NOT EXISTS "${ASTROLABE_PNGDEC}/PNGdec.cpp")
    message(FATAL_ERROR "Missing PNGdec. Run pio pkg install for waveshare_s3_175 or set ASTROLABE_PNGDEC.")
endif()

file(GLOB PNGDEC_SRCS "${ASTROLABE_PNGDEC}/*.cpp" "${ASTROLABE_PNGDEC}/*.c")

idf_component_register(
    SRCS
        "main.c"
        "faculty175_board.c"
        "faculty175_board_id.c"
        "faculty175_touch.c"
        "faculty175_gesture.c"
        "faculty175_pmu.cpp"
        "atom_serial.c"
        "atom_device_auth.c"
        "atom_ota.c"
        "atom_qa.c"
        "atom_voice.c"
        "atom_listen.c"
        "atom_faculty.c"
        "atom_faculty_roster.c"
        "atom_faculty_png.cpp"
        ${PNGDEC_SRCS}
    INCLUDE_DIRS "." "../../include" "${ASTROLABE_PNGDEC}"
    REQUIRES
        driver
        esp_event
        esp_http_client
        esp_lcd
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
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE XPOWERS_CHIP_AXP2101)

target_compile_definitions(${COMPONENT_LIB} PRIVATE __LINUX__ PNG_MAX_BUFFERED_PIXELS=8194)
