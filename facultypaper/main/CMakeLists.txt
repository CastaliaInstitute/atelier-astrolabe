set(ASTROLABE_PNGDEC "${CMAKE_CURRENT_LIST_DIR}/../../.pio/libdeps/waveshare_s3_185/PNGdec/src")
if(NOT EXISTS "${ASTROLABE_PNGDEC}/PNGdec.cpp")
    message(FATAL_ERROR "Missing PNGdec. Run pio pkg install for waveshare_s3_185 or set ASTROLABE_PNGDEC.")
endif()

file(GLOB PNGDEC_SRCS "${ASTROLABE_PNGDEC}/*.cpp" "${ASTROLABE_PNGDEC}/*.c")

idf_component_register(
    SRCS
        "main.c"
        "paper_board.cpp"
        "paper_epd.c"
        "paper_serial.c"
        "paper_ota.c"
        "paper_qa.c"
        "paper_memory.c"
        "paper_faculty.c"
        "paper_faculty_png.cpp"
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
        app_update
        fatfs
        json
        minimp3
        nvs_flash
        mbedtls
        esp_new_jpeg
        astrolabe_audio_pipeline
        sdmmc
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE __LINUX__ PNG_MAX_BUFFERED_PIXELS=8194)
