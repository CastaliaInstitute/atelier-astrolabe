cmake_minimum_required(VERSION 3.16)

set(ASTROLABE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
set(ASTROLABE_ARDUINO_COMPONENT "$ENV{PLATFORMIO_PACKAGES_DIR}/framework-arduinoespressif32")
if(NOT EXISTS "${ASTROLABE_ARDUINO_COMPONENT}/CMakeLists.txt")
  set(ASTROLABE_ARDUINO_COMPONENT "$ENV{HOME}/.platformio/packages/framework-arduinoespressif32")
endif()
if(EXISTS "${ASTROLABE_ARDUINO_COMPONENT}/CMakeLists.txt")
  list(APPEND EXTRA_COMPONENT_DIRS "${ASTROLABE_ARDUINO_COMPONENT}")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(astrolabe-cspot)
