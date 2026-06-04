cmake_minimum_required(VERSION 3.5)

set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../lib/astrolabe_audio_pipeline"
    "${CMAKE_CURRENT_LIST_DIR}/../lib/astrolabe_time"
    "${CMAKE_CURRENT_LIST_DIR}/../lib/minimp3"
    "${CMAKE_CURRENT_LIST_DIR}/../vendor/ESP32-S3-Touch-AMOLED-1.8/examples/ESP-IDF-v5.3.2/01_AXP2101/components/XPowersLib")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

add_compile_options(-fdiagnostics-color=always -Wno-error=format= -Wno-format)

project(astrolabe_faculty175)
