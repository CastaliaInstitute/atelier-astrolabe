set(ASTROLABE_PNGDEC "${CMAKE_CURRENT_LIST_DIR}/../../.pio/libdeps/waveshare_s3_185/PNGdec/src")
if(NOT EXISTS "${ASTROLABE_PNGDEC}/PNGdec.cpp")
    message(FATAL_ERROR "Missing PNGdec. Run pio pkg install for waveshare_s3_185 or set ASTROLABE_PNGDEC.")
endif()

file(GLOB PNGDEC_SRCS "${ASTROLABE_PNGDEC}/*.cpp" "${ASTROLABE_PNGDEC}/*.c")

idf_component_register(
    SRCS
        "main.c"
        "atom_board.c"
        "atom_serial.c"
        "atom_qa.c"
        "atom_voice.c"
        "atom_listen.c"
        "atom_faculty.c"
        "atom_faculty_png.cpp"
        ${PNGDEC_SRCS}
    INCLUDE_DIRS "." "../../include" "${ASTROLABE_PNGDEC}"
    REQUIRES
        driver
        esp_codec_dev
        esp_event
        esp_http_client
        esp_lcd
        esp_netif
        esp-tls
        esp_wifi
        minimp3
        nvs_flash
        mbedtls
        esp_new_jpeg
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE __LINUX__ PNG_MAX_BUFFERED_PIXELS=8194)
