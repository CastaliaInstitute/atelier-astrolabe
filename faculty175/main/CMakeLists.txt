set(ASTROLABE_PNGDEC "${CMAKE_CURRENT_LIST_DIR}/../../lib/PNGdec/src")
if(NOT EXISTS "${ASTROLABE_PNGDEC}/PNGdec.cpp")
    message(FATAL_ERROR "Missing PNGdec. Run pio pkg install for waveshare_s3_175 or set ASTROLABE_PNGDEC.")
endif()

file(GLOB PNGDEC_SRCS "${ASTROLABE_PNGDEC}/*.cpp" "${ASTROLABE_PNGDEC}/*.c")
file(GLOB FACULTY175_FACE_SRCS "${CMAKE_CURRENT_LIST_DIR}/faculty175_face_*.c")
list(REMOVE_ITEM FACULTY175_FACE_SRCS "${CMAKE_CURRENT_LIST_DIR}/faculty175_face_pocketwatch.c")
list(REMOVE_ITEM FACULTY175_FACE_SRCS
    "${CMAKE_CURRENT_LIST_DIR}/faculty175_face_bongo.c"
    "${CMAKE_CURRENT_LIST_DIR}/faculty175_face_bowl.c"
    "${CMAKE_CURRENT_LIST_DIR}/faculty175_face_chakra.c"
    "${CMAKE_CURRENT_LIST_DIR}/faculty175_face_chord.c"
    "${CMAKE_CURRENT_LIST_DIR}/faculty175_face_drone.c"
    "${CMAKE_CURRENT_LIST_DIR}/faculty175_face_enochian.c"
    "${CMAKE_CURRENT_LIST_DIR}/faculty175_face_geomancy.c"
    "${CMAKE_CURRENT_LIST_DIR}/faculty175_face_almanac.c"
    "${CMAKE_CURRENT_LIST_DIR}/faculty175_face_phenology.c"
    "${CMAKE_CURRENT_LIST_DIR}/faculty175_face_kalimba.c"
    "${CMAKE_CURRENT_LIST_DIR}/faculty175_face_level.c"
    "${CMAKE_CURRENT_LIST_DIR}/faculty175_face_luopan.c"
    "${CMAKE_CURRENT_LIST_DIR}/faculty175_face_ocarina.c"
    "${CMAKE_CURRENT_LIST_DIR}/faculty175_face_orient.c"
    "${CMAKE_CURRENT_LIST_DIR}/faculty175_face_pandrum.c"
    "${CMAKE_CURRENT_LIST_DIR}/faculty175_face_piano.c"
    "${CMAKE_CURRENT_LIST_DIR}/faculty175_face_pitch.c"
    "${CMAKE_CURRENT_LIST_DIR}/faculty175_face_pythia.c"
    "${CMAKE_CURRENT_LIST_DIR}/faculty175_face_scale.c"
    "${CMAKE_CURRENT_LIST_DIR}/faculty175_face_spectrum.c"
    "${CMAKE_CURRENT_LIST_DIR}/faculty175_face_tuning.c"
)

idf_component_register(
    SRCS
        "main.c"
        "faculty175_board.c"
        "faculty175_board_id.c"
        "faculty175_touch.c"
        "faculty175_gesture.c"
        "faculty175_motion.c"
        "faculty175_pmu.cpp"
        "faculty175_serial.c"
        "faculty175_screen_http.c"
        "faculty175_wifi_settings.c"
        "faculty175_wifi_monitor.c"
        "faculty175_wifi_lab.c"
        "faculty175_lvgl.c"
        "faculty175_face_pocketwatch.c"
        "faculty175_face_incidents.c"
        "faculty175_pocketwatch.c"
        "faculty175_charts.c"
        "faculty175_ble.c"
        "faculty175_apocalypso.c"
        "faculty175_device_auth.c"
        "faculty175_device_settings.c"
        ${FACULTY175_FACE_SRCS}
        "faculty175_faces.c"
        "faculty175_face_profile.c"
        "../../lib/astrolabe_display_core/astrolabe_round_bezel.c"
        "faculty175_ota.c"
        "faculty175_qa.c"
        "faculty175_listen.c"
        "faculty175_voice.c"
        "faculty175_faculty.c"
        "faculty175_quotes.c"
        "faculty175_rocket.c"
        "faculty175_faculty_roster.c"
        "faculty175_faculty_png.cpp"
        "faculty175_face_tarot_image.cpp"
        "faculty175_face_tarot_spiffs_image.cpp"
        ${PNGDEC_SRCS}
    INCLUDE_DIRS "." "../../include" "../../lib/astrolabe_display_core/include" "${ASTROLABE_PNGDEC}"
    REQUIRES
        driver
        esp_event
        esp_http_client
        esp_http_server
        esp_lcd
        lvgl
        lwip
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
        bt
    EMBED_FILES
        "assets/pocketwatch_default.rgb565"
        "assets/maze_466.png"
        "assets/v5-a.darwin-right.png"
        "assets/v5-a.plato-right.png"
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE XPOWERS_CHIP_AXP2101)
target_compile_definitions(${COMPONENT_LIB} PUBLIC LV_CONF_INCLUDE_SIMPLE=1)

target_compile_definitions(${COMPONENT_LIB} PRIVATE __LINUX__ PNG_MAX_BUFFERED_PIXELS=8194)
