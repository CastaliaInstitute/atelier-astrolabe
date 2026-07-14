cmake_minimum_required(VERSION 3.20)

project(astrolabe_web_sim C CXX)

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
)
FetchContent_MakeAvailable(lvgl)

file(GLOB FACULTY175_FACE_SRCS
  "${CMAKE_CURRENT_SOURCE_DIR}/../../astrolabe175c/main/faculty175_face_*.c"
)
list(REMOVE_ITEM FACULTY175_FACE_SRCS
  "${CMAKE_CURRENT_SOURCE_DIR}/../../astrolabe175c/main/faculty175_face_deathstar.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/../../astrolabe175c/main/faculty175_face_incidents.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/../../astrolabe175c/main/faculty175_face_maze.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/../../astrolabe175c/main/faculty175_face_watcher.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/../../astrolabe175c/main/faculty175_face_wifilab.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/../../astrolabe175c/main/faculty175_face_hid.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/../../astrolabe175c/main/faculty175_face_native.c"
)

add_executable(astrolabe-web-sim
  src/main.c
  src/faculty175_websim_stubs.c
  ${FACULTY175_FACE_SRCS}
  ../../astrolabe175c/main/faculty175_faces.c
  ../../lib/astrolabe_display_core/astrolabe_round_bezel.c
)

target_include_directories(astrolabe-web-sim PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${lvgl_SOURCE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/faculty175_compat"
  "${CMAKE_CURRENT_SOURCE_DIR}/faculty175_compat/freertos"
  "${CMAKE_CURRENT_SOURCE_DIR}/faculty175_compat/driver"
  "${CMAKE_CURRENT_SOURCE_DIR}/../../astrolabe175c/main"
  "${CMAKE_CURRENT_SOURCE_DIR}/../../include"
  "${CMAKE_CURRENT_SOURCE_DIR}/../../lib/astrolabe_display_core/include"
  "${CMAKE_CURRENT_SOURCE_DIR}/../../lib/astrolabe_time/include"
)

target_link_libraries(astrolabe-web-sim PRIVATE lvgl)

target_compile_definitions(astrolabe-web-sim PRIVATE
  LV_CONF_INCLUDE_SIMPLE=1
  ASTROLABE_WEB_SIM=1
)

target_compile_options(astrolabe-web-sim PRIVATE
  "$<$<COMPILE_LANGUAGE:C>:-include${CMAKE_CURRENT_SOURCE_DIR}/faculty175_compat/lvgl_api_compat.h>"
)

target_link_options(astrolabe-web-sim PRIVATE
  "SHELL:-s WASM=1"
  "SHELL:-s EXPORTED_FUNCTIONS=['_main','_astrolabe_web_set_face','_astrolabe_web_face_count','_astrolabe_web_face_name','_astrolabe_web_face_slug','_astrolabe_web_current_face','_astrolabe_web_voice_state','_astrolabe_web_button_press']"
  "SHELL:-s EXPORTED_RUNTIME_METHODS=['ccall','UTF8ToString']"
  "SHELL:-s ALLOW_MEMORY_GROWTH=1"
  "SHELL:--shell-file ${CMAKE_CURRENT_SOURCE_DIR}/public/shell.html"
)

set_target_properties(astrolabe-web-sim PROPERTIES
  OUTPUT_NAME "astrolabe-web-sim"
  SUFFIX ".html"
  LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/public/shell.html"
)

add_custom_command(TARGET astrolabe-web-sim POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_CURRENT_SOURCE_DIR}/public/manifest.webmanifest"
          "$<TARGET_FILE_DIR:astrolabe-web-sim>/manifest.webmanifest"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_CURRENT_SOURCE_DIR}/public/sw.js"
          "$<TARGET_FILE_DIR:astrolabe-web-sim>/sw.js"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_CURRENT_SOURCE_DIR}/public/icon.svg"
          "$<TARGET_FILE_DIR:astrolabe-web-sim>/icon.svg"
)
