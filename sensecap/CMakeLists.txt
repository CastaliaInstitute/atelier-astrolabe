cmake_minimum_required(VERSION 3.5)

if(NOT DEFINED ASTROLABE_SENSECAP_FW_PATH OR "${ASTROLABE_SENSECAP_FW_PATH}" STREQUAL "")
    set(ASTROLABE_SENSECAP_FW_PATH
        "${CMAKE_CURRENT_LIST_DIR}/../../sensecap/SenseCAP-Watcher-Firmware"
        CACHE PATH "Path to the SenseCAP Watcher firmware checkout")
endif()

get_filename_component(ASTROLABE_SENSECAP_FW_PATH
    "${ASTROLABE_SENSECAP_FW_PATH}" ABSOLUTE)

if(NOT EXISTS "${ASTROLABE_SENSECAP_FW_PATH}/components/sensecap-watcher")
    message(FATAL_ERROR
        "SenseCAP Watcher firmware components not found at "
        "${ASTROLABE_SENSECAP_FW_PATH}. Set -DASTROLABE_SENSECAP_FW_PATH=/path/to/SenseCAP-Watcher-Firmware")
endif()

set(EXTRA_COMPONENT_DIRS
    "${ASTROLABE_SENSECAP_FW_PATH}/components/sensecap-watcher"
    "${ASTROLABE_SENSECAP_FW_PATH}/components/sscma_client"
    "${ASTROLABE_SENSECAP_FW_PATH}/components/esp_io_expander_pca95xx_16bit"
    "${ASTROLABE_SENSECAP_FW_PATH}/components/esp_lvgl_port"
    "${ASTROLABE_SENSECAP_FW_PATH}/components/esp_jpeg_simd"
    "${CMAKE_CURRENT_LIST_DIR}/../lib/minimp3")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

add_compile_options(-fdiagnostics-color=always -Wno-error=format= -Wno-format)

project(astrolabe_sensecap)
