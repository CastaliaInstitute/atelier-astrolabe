set(ASTROLABE_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")
set(ASTROLABE_SKETCH "${ASTROLABE_ROOT}/sketches/Astrolabe")
set(ASTROLABE_WEB_SIM "${ASTROLABE_ROOT}/tools/web-sim")

file(GLOB_RECURSE ASTROLABE_FACE_SRCS
    "${ASTROLABE_SKETCH}/faces/*.cpp")

idf_component_register(
    SRCS
        "p4_real_ui.cpp"
        "p4_faculty_assets.cpp"
        "${ASTROLABE_WEB_SIM}/src/arduino_compat.cpp"
        "${ASTROLABE_WEB_SIM}/src/device_stubs.cpp"
        "${ASTROLABE_SKETCH}/pm_display.cpp"
        "${ASTROLABE_SKETCH}/pm_heap.cpp"
        "${ASTROLABE_SKETCH}/pm_presence_graph.cpp"
        "${ASTROLABE_SKETCH}/third_party/qrcodegen/qrcodegen.c"
        ${ASTROLABE_FACE_SRCS}
    INCLUDE_DIRS
        "."
        "${ASTROLABE_WEB_SIM}"
        "${ASTROLABE_WEB_SIM}/arduino_compat"
        "${ASTROLABE_SKETCH}"
        "${ASTROLABE_SKETCH}/third_party/qrcodegen"
        "${ASTROLABE_ROOT}/ui"
    REQUIRES
        lvgl
        esp_timer
        esp_common
        heap
        freertos
        espressif__libpng)

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    ASTROLABE_P4_TARGET=1
    MYNAH_DEBUG_GESTURES=0
    MYNAH_HUE_HOME_ONLY=0)

target_compile_options(${COMPONENT_LIB} PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:-include${ASTROLABE_WEB_SIM}/arduino_compat/Arduino.h>"
    "$<$<COMPILE_LANGUAGE:CXX>:-Wno-error=class-memaccess>"
    "$<$<COMPILE_LANGUAGE:CXX>:-Wno-error=format-truncation>")
