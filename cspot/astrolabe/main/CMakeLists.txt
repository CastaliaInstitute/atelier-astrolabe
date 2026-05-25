set(ASTROLABE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../..")

idf_component_register(
    SRCS
      "main.cpp"
      "AstrolabeSpeakerAudioSink.cpp"
      "${ASTROLABE_ROOT}/lib/waveshare_board_audio/es8311.c"
    INCLUDE_DIRS
      "."
      "${ASTROLABE_ROOT}/sketches/Astrolabe"
      "${ASTROLABE_ROOT}/lib/waveshare_board_audio"
    REQUIRES
      esp_wifi
      esp_netif
      esp_event
      esp_http_server
      mdns
      nvs_flash
      spiffs)

set(BELL_DISABLE_MQTT ON CACHE BOOL "" FORCE)
set(BELL_DISABLE_SINKS ON CACHE BOOL "" FORCE)
set(BELL_DISABLE_CODECS ON CACHE BOOL "" FORCE)
set(BELL_SINK_ALSA OFF CACHE BOOL "" FORCE)
set(BELL_SINK_PORTAUDIO OFF CACHE BOOL "" FORCE)
add_subdirectory("${ASTROLABE_ROOT}/third_party/cspot/cspot" "${CMAKE_CURRENT_BINARY_DIR}/cspot")

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/cspot/protobuf")
file(COPY "${ASTROLABE_ROOT}/cspot/astrolabe/generated/protobuf/"
     DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/cspot/protobuf")
target_include_directories(cspot PUBLIC "${ASTROLABE_ROOT}/cspot/astrolabe/generated")
target_include_directories(bell PUBLIC "${ASTROLABE_ROOT}/third_party/cspot/cspot/bell/external/libhelix-mp3")
target_compile_options(bell PRIVATE -Wno-error=overloaded-virtual)
add_dependencies(${COMPONENT_LIB} generate_proto_sources)
target_link_libraries(${COMPONENT_LIB} PUBLIC cspot)
target_include_directories(${COMPONENT_LIB} PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/cspot"
    "${ASTROLABE_ROOT}/cspot/astrolabe/generated")
target_compile_options(${COMPONENT_LIB} PRIVATE -std=gnu++20)
