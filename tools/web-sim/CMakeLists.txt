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

add_executable(astrolabe-web-sim
  src/arduino_compat.cpp
  src/device_main.cpp
  ../../sketches/Astrolabe/pm_display.cpp
  ../../sketches/Astrolabe/faces/pm_clock.cpp
  ../../sketches/Astrolabe/faces/shared/pm_face_draw.cpp
  ../../sketches/Astrolabe/faces/shared/pm_circadian_hue.cpp
  ../../sketches/Astrolabe/faces/apocalypso/pm_face_apocalypso.cpp
  ../../sketches/Astrolabe/faces/alethiometer/pm_alethiometer_emoji_glyphs.cpp
  ../../sketches/Astrolabe/faces/alethiometer/pm_face_alethiometer.cpp
  ../../sketches/Astrolabe/faces/astrology/pm_face_astrology.cpp
  ../../sketches/Astrolabe/faces/astrology/planet_glyphs.cpp
  ../../sketches/Astrolabe/faces/astrology/pm_zodiac_glyphs.cpp
  ../../sketches/Astrolabe/faces/astrology/zodiac_glyphs.cpp
  ../../sketches/Astrolabe/faces/biometrics/pm_face_biometrics.cpp
  ../../sketches/Astrolabe/faces/bongo/pm_face_bongo.cpp
  ../../sketches/Astrolabe/faces/calcifer/pm_face_calcifer.cpp
  ../../sketches/Astrolabe/faces/castalia/pm_face_castalia.cpp
  ../../sketches/Astrolabe/faces/chakra/pm_chakra_glyphs.cpp
  ../../sketches/Astrolabe/faces/chakra/chakra_glyphs.cpp
  ../../sketches/Astrolabe/faces/chakra/pm_face_chakra.cpp
  ../../sketches/Astrolabe/faces/classic_analog/pm_face_classic_analog.cpp
  ../../sketches/Astrolabe/faces/digital/pm_face_digital.cpp
  ../../sketches/Astrolabe/faces/enochian_angel/pm_face_enochian_angel.cpp
  ../../sketches/Astrolabe/faces/faculty/pm_face_faculty.cpp
  ../../sketches/Astrolabe/faces/focus/pm_face_focus.cpp
  ../../sketches/Astrolabe/faces/geomancy/pm_face_geomancy.cpp
  ../../sketches/Astrolabe/faces/globe/pm_face_globe.cpp
  ../../sketches/Astrolabe/faces/home/pm_face_home_briefing.cpp
  ../../sketches/Astrolabe/faces/lenormand/pm_face_lenormand.cpp
  ../../sketches/Astrolabe/faces/level/pm_face_level.cpp
  ../../sketches/Astrolabe/faces/live_transits/pm_face_live_transits.cpp
  ../../sketches/Astrolabe/faces/luopan/pm_face_luopan.cpp
  ../../sketches/Astrolabe/faces/moon/pm_face_moon.cpp
  ../../sketches/Astrolabe/faces/moon/pm_moon_draw.cpp
  ../../sketches/Astrolabe/faces/notes/pm_face_notes.cpp
  ../../sketches/Astrolabe/faces/ocarina/pm_face_ocarina.cpp
  ../../sketches/Astrolabe/faces/orientation/pm_face_orientation.cpp
  ../../sketches/Astrolabe/faces/pandrum/pm_face_pandrum.cpp
  ../../sketches/Astrolabe/faces/piano/pm_face_piano.cpp
  ../../sketches/Astrolabe/faces/pythia/pm_face_pythia.cpp
  ../../sketches/Astrolabe/faces/question_day/pm_face_question_day.cpp
  ../../sketches/Astrolabe/faces/quotes/pm_face_quotes.cpp
  ../../sketches/Astrolabe/faces/radar/pm_face_radar.cpp
  ../../sketches/Astrolabe/faces/rocket/pm_face_rocket.cpp
  ../../sketches/Astrolabe/faces/runes/pm_face_runes.cpp
  ../../sketches/Astrolabe/faces/settings/pm_face_settings_battery.cpp
  ../../sketches/Astrolabe/faces/settings/pm_face_settings_sleep.cpp
  ../../sketches/Astrolabe/faces/settings/pm_face_settings_variant.cpp
  ../../sketches/Astrolabe/faces/settings/pm_face_settings_wifi.cpp
  ../../sketches/Astrolabe/faces/sky/pm_face_sky.cpp
  ../../sketches/Astrolabe/faces/spectrum/pm_face_spectrum.cpp
  ../../sketches/Astrolabe/faces/spectrum/pm_face_spectrum_viz.cpp
  ../../sketches/Astrolabe/faces/spotify/pm_face_spotify.cpp
  ../../sketches/Astrolabe/faces/synastry/pm_face_synastry.cpp
  ../../sketches/Astrolabe/faces/tarot/pm_face_tarot.cpp
  ../../sketches/Astrolabe/faces/tibetan_bowl/pm_face_tibetan_bowl.cpp
  ../../sketches/Astrolabe/faces/tuning/pm_face_tuning.cpp
  ../../sketches/Astrolabe/faces/weather/pm_face_weather.cpp
  ../../sketches/Astrolabe/pm_presence_graph.cpp
  ../../sketches/Astrolabe/third_party/qrcodegen/qrcodegen.c
  src/device_stubs.cpp
)

target_include_directories(astrolabe-web-sim PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/arduino_compat"
  "${CMAKE_CURRENT_SOURCE_DIR}/../../sketches/Astrolabe"
  "${CMAKE_CURRENT_SOURCE_DIR}/../../sketches/Astrolabe/third_party/qrcodegen"
  "${CMAKE_CURRENT_SOURCE_DIR}/../../ui"
)

target_link_libraries(astrolabe-web-sim PRIVATE lvgl)

target_compile_definitions(astrolabe-web-sim PRIVATE
  LV_CONF_INCLUDE_SIMPLE=1
  ASTROLABE_WEB_SIM=1
  MYNAH_DEBUG_GESTURES=0
  MYNAH_HUE_HOME_ONLY=0
)

target_compile_options(astrolabe-web-sim PRIVATE
  "$<$<COMPILE_LANGUAGE:CXX>:-include${CMAKE_CURRENT_SOURCE_DIR}/arduino_compat/Arduino.h>"
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
