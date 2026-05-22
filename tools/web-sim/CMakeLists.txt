cmake_minimum_required(VERSION 3.20)

project(astrolabe_web_sim C)

include(FetchContent)

if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()

set(LV_CONF_INCLUDE_SIMPLE ON CACHE BOOL "" FORCE)
set(LV_CONF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h" CACHE FILEPATH "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
  lvgl
  URL https://github.com/lvgl/lvgl/archive/refs/tags/v9.3.0.tar.gz
  URL_HASH SHA256=4933becfd3603b29158a5d04138139582836ef2bc17beb6c39dccda9cb0d32e7
)
FetchContent_MakeAvailable(lvgl)

add_executable(astrolabe-web-sim
  src/main.c
  ../../ui/astrolabe_ui.c
)

target_include_directories(astrolabe-web-sim PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/../../ui"
)

target_link_libraries(astrolabe-web-sim PRIVATE lvgl)

target_compile_definitions(astrolabe-web-sim PRIVATE
  LV_CONF_INCLUDE_SIMPLE=1
)

target_link_options(astrolabe-web-sim PRIVATE
  "SHELL:-s WASM=1"
  "SHELL:-s EXPORTED_FUNCTIONS=['_main','_astrolabe_web_set_face']"
  "SHELL:-s EXPORTED_RUNTIME_METHODS=['ccall']"
  "SHELL:-s ALLOW_MEMORY_GROWTH=1"
  "SHELL:--shell-file ${CMAKE_CURRENT_SOURCE_DIR}/public/shell.html"
)

set_target_properties(astrolabe-web-sim PROPERTIES
  OUTPUT_NAME "astrolabe-web-sim"
  SUFFIX ".html"
  LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/public/shell.html"
)
