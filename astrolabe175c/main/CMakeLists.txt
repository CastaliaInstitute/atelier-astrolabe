set(ASTROLABE_PNGDEC "${CMAKE_CURRENT_LIST_DIR}/../../lib/PNGdec/src")
if(NOT EXISTS "${ASTROLABE_PNGDEC}/PNGdec.cpp")
    message(FATAL_ERROR "Missing lib/PNGdec/src. Restore the repository dependency or set ASTROLABE_PNGDEC.")
endif()

if(NOT DEFINED ASTROLABE175C_BUILD_VARIANT)
    set(ASTROLABE175C_BUILD_VARIANT "faculty")
endif()
string(TOLOWER "${ASTROLABE175C_BUILD_VARIANT}" ASTROLABE175C_BUILD_VARIANT)
if(NOT ASTROLABE175C_BUILD_VARIANT STREQUAL "faculty" AND
   NOT ASTROLABE175C_BUILD_VARIANT STREQUAL "cyber" AND
   NOT ASTROLABE175C_BUILD_VARIANT STREQUAL "lunasay" AND
   NOT ASTROLABE175C_BUILD_VARIANT STREQUAL "claw")
    message(FATAL_ERROR "ASTROLABE175C_BUILD_VARIANT must be faculty, cyber, lunasay, or claw")
endif()
set(ASTROLABE175C_CYBER_FEATURES 0)
set(ASTROLABE175C_FACTORY_RECOVERY 0)
set(ASTROLABE175C_FIRMWARE_VARIANT "Faculty")
set(ASTROLABE175C_OTA_CHANNEL "astrolabe-faculty-amoled175")
set(FACULTY175_EMBED_TXTFILES)
if(ASTROLABE175C_BUILD_VARIANT STREQUAL "cyber")
    set(ASTROLABE175C_CYBER_FEATURES 1)
    set(ASTROLABE175C_FIRMWARE_VARIANT "Cyber")
    set(ASTROLABE175C_OTA_CHANNEL "astrolabe-cyber-175")
    list(APPEND FACULTY175_EMBED_TXTFILES
        "faculty175_km_page.html"
        "astrolabe-install.sh")
elseif(ASTROLABE175C_BUILD_VARIANT STREQUAL "lunasay")
    set(ASTROLABE175C_FIRMWARE_VARIANT "LunaSay")
    set(ASTROLABE175C_OTA_CHANNEL "astrolabe-lunasay-175")
elseif(ASTROLABE175C_BUILD_VARIANT STREQUAL "claw")
    set(ASTROLABE175C_FACTORY_RECOVERY 1)
    set(ASTROLABE175C_FIRMWARE_VARIANT "Claw")
    set(ASTROLABE175C_OTA_CHANNEL "astrolabe-claw-175")
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
        "faculty175_storage.c"
        "faculty175_usb.c"
        "faculty175_usb_ncm.c"
        "faculty175_km.c"
        "faculty175_km_http.c"
        "faculty175_usb_screen.c"
        "faculty175_gesture.c"
        "faculty175_motion.c"
        "faculty175_breath.c"
        "faculty175_pmu.cpp"
        "faculty175_deep_sleep.c"
        "faculty175_power_metrics.c"
        "faculty175_power_history.c"
        "faculty175_serial.c"
        "faculty175_screen_http.c"
        "faculty175_wifi_settings.c"
        "faculty175_wifi_monitor.c"
        "faculty175_wifi_lab.c"
        "faculty175_lvgl.c"
        "faculty175_face_pocketwatch.c"
        "faculty175_face_incidents.c"
        "faculty175_pocketwatch.c"
        "faculty175_astro_math.c"
        "faculty175_human_design_math.c"
        "faculty175_charts.c"
        "faculty175_ephemeris.c"
        "faculty175_cycle_arcs.c"
        "faculty175_cycle_health.c"
        "faculty175_relationship_weather.c"
        "faculty175_family.c"
        "faculty175_ble.c"
        "faculty175_ring.c"
        "faculty175_apocalypso.c"
        "faculty175_device_auth.c"
        "faculty175_device_settings.c"
        "faculty175_research.c"
        "faculty175_spotify.c"
        ${FACULTY175_FACE_SRCS}
        "faculty175_face_human_design.c"
        "faculty175_faces.c"
        "faculty175_face_profile.c"
        "faculty175_variant.c"
        "faculty175_rotary_state.c"
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
        "faculty175_face_solar_image.cpp"
        "faculty175_face_tarot_spiffs_image.cpp"
        ${PNGDEC_SRCS}
    INCLUDE_DIRS "." "../../include" "../../lib/astrolabe_display_core/include" "${ASTROLABE_PNGDEC}"
    REQUIRES
        driver
        esp_driver_usb_serial_jtag
        console
        esp_event
        esp_http_client
        esp_http_server
        esp_lcd
        lvgl
        lwip
        espressif__mdns
        espressif__esp_lcd_touch
        waveshare__esp_lcd_touch_cst9217
        esp_netif
        esp-tls
        esp_wifi
        fatfs
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
        wear_levelling
    EMBED_FILES
        "assets/pocketwatch_default.rgb565"
        "assets/maze_466.png"
        "assets/v5-a.darwin-right.png"
        "assets/v5-a.plato-right.png"
    EMBED_TXTFILES
        ${FACULTY175_EMBED_TXTFILES}
)

if(ASTROLABE175C_CYBER_FEATURES)
    target_add_binary_data(${COMPONENT_LIB}
        "${CMAKE_CURRENT_LIST_DIR}/../../scripts/astrolabe_pi_screen.py"
        TEXT)
endif()

target_compile_definitions(${COMPONENT_LIB} PRIVATE XPOWERS_CHIP_AXP2101)
target_compile_definitions(${COMPONENT_LIB} PUBLIC LV_CONF_INCLUDE_SIMPLE=1)

target_compile_definitions(${COMPONENT_LIB} PRIVATE __LINUX__ PNG_MAX_BUFFERED_PIXELS=8194)
if(NOT DEFINED ASTROLABE_USB_OTA_DEMO_BOOT)
    set(ASTROLABE_USB_OTA_DEMO_BOOT 0)
endif()
if(ASTROLABE175C_CYBER_FEATURES OR ASTROLABE175C_FACTORY_RECOVERY)
    set(ASTROLABE_USB_RUNTIME_ENABLED 1)
else()
    set(ASTROLABE_USB_RUNTIME_ENABLED 0)
endif()
set(ASTROLABE_KM_ENABLED ${ASTROLABE175C_CYBER_FEATURES})
if(NOT DEFINED ASTROLABE_OTA_AUTO_INTERVAL_S)
    set(ASTROLABE_OTA_AUTO_INTERVAL_S 60)
endif()
if(NOT ASTROLABE_OTA_AUTO_INTERVAL_S MATCHES "^[0-9]+$" OR ASTROLABE_OTA_AUTO_INTERVAL_S LESS 60)
    message(FATAL_ERROR "ASTROLABE_OTA_AUTO_INTERVAL_S must be an integer of at least 60 seconds")
endif()
target_compile_definitions(${COMPONENT_LIB} PRIVATE ASTROLABE_USB_OTA_DEMO_BOOT=${ASTROLABE_USB_OTA_DEMO_BOOT})
target_compile_definitions(${COMPONENT_LIB} PRIVATE ASTROLABE_USB_RUNTIME_ENABLED=${ASTROLABE_USB_RUNTIME_ENABLED})
target_compile_definitions(${COMPONENT_LIB} PRIVATE ASTROLABE_KM_ENABLED=${ASTROLABE_KM_ENABLED})
target_compile_definitions(${COMPONENT_LIB} PRIVATE ASTROLABE_CYBER_FEATURES=${ASTROLABE175C_CYBER_FEATURES})
target_compile_definitions(${COMPONENT_LIB} PRIVATE ASTROLABE_FACTORY_RECOVERY=${ASTROLABE175C_FACTORY_RECOVERY})
target_compile_definitions(${COMPONENT_LIB} PRIVATE ASTROLABE_FIRMWARE_VARIANT="${ASTROLABE175C_FIRMWARE_VARIANT}")
target_compile_definitions(${COMPONENT_LIB} PRIVATE ASTROLABE_FACULTY175_OTA_CHANNEL="${ASTROLABE175C_OTA_CHANNEL}")
if(ASTROLABE175C_CYBER_FEATURES)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE ASTROLABE_FORCE_VARIANT_CYBER=1)
elseif(ASTROLABE175C_BUILD_VARIANT STREQUAL "lunasay")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE ASTROLABE_FORCE_VARIANT_LUNASAY=1)
elseif(ASTROLABE175C_BUILD_VARIANT STREQUAL "claw")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE ASTROLABE_FORCE_VARIANT_CLAW=1 ASTROLABE_CLAW_VARIANT=1)
endif()
target_compile_definitions(${COMPONENT_LIB} PRIVATE ASTROLABE_OTA_AUTO_INTERVAL_S=${ASTROLABE_OTA_AUTO_INTERVAL_S})
if(DEFINED ASTROLABE_VOICE_HTTP_URL AND NOT "${ASTROLABE_VOICE_HTTP_URL}" STREQUAL "")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE MYNAH_VOICE_HTTP_URL="${ASTROLABE_VOICE_HTTP_URL}")
endif()
if(DEFINED ASTROLABE_VOICE_STREAM_URL AND NOT "${ASTROLABE_VOICE_STREAM_URL}" STREQUAL "")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE MYNAH_VOICE_STREAM_URL="${ASTROLABE_VOICE_STREAM_URL}")
endif()
