cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../lib/astrolabe_audio_pipeline"
    "${CMAKE_CURRENT_LIST_DIR}/../lib/astrolabe_time"
    "${CMAKE_CURRENT_LIST_DIR}/../lib/waveshare_board_audio"
    "${CMAKE_CURRENT_LIST_DIR}/../lib/minimp3")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(astrolabe)

# Match PlatformIO: USB Serial/JTAG (HWCDC), not TinyUSB device CDC.
add_compile_definitions(ARDUINO_USB_CDC_ON_BOOT=1 ARDUINO_USB_MODE=1)
