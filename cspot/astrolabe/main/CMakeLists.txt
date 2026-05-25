set(ASTROLABE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../..")

set(ASTROLABE_ARDUINO_GFX_DIR "" CACHE PATH
    "Arduino_GFX source directory for the Waveshare 1.85 display")
if(NOT ASTROLABE_ARDUINO_GFX_DIR)
  foreach(candidate
      "${ASTROLABE_ROOT}/cspot/astrolabe/.pio/libdeps/astrolabe-cspot-s3-185-v1/GFX Library for Arduino/src"
      "${ASTROLABE_ROOT}/cspot/astrolabe/.pio/libdeps/astrolabe-cspot-s3-185-v2/GFX Library for Arduino/src"
      "${ASTROLABE_ROOT}/.pio/libdeps/waveshare_s3_185_v2/GFX_Library_for_Arduino/src"
      "${ASTROLABE_ROOT}/.pio/libdeps/waveshare_s3_175/GFX_Library_for_Arduino/src")
    if(EXISTS "${candidate}")
      set(ASTROLABE_ARDUINO_GFX_DIR "${candidate}")
      break()
    endif()
  endforeach()
endif()

if(NOT EXISTS "${ASTROLABE_ARDUINO_GFX_DIR}")
  message(FATAL_ERROR
      "Missing Arduino_GFX sources. Run `pio pkg install -e astrolabe-cspot-s3-185-v1`, "
      "or configure with -DASTROLABE_ARDUINO_GFX_DIR=/path/to/GFX/src.")
endif()

set(ASTROLABE_JPEGDEC_DIR "" CACHE PATH
    "JPEGDEC source directory for album-art decoding")
if(NOT ASTROLABE_JPEGDEC_DIR)
  foreach(candidate
      "${ASTROLABE_ROOT}/cspot/astrolabe/.pio/libdeps/astrolabe-cspot-s3-185-v1/JPEGDEC/src"
      "${ASTROLABE_ROOT}/cspot/astrolabe/.pio/libdeps/astrolabe-cspot-s3-185-v2/JPEGDEC/src")
    if(EXISTS "${candidate}")
      set(ASTROLABE_JPEGDEC_DIR "${candidate}")
      break()
    endif()
  endforeach()
endif()

idf_component_register(
    SRCS
	      "main.cpp"
	      "AstrolabeSpeakerAudioSink.cpp"
	      "AstrolabeAudioVisualizer.cpp"
	      "${ASTROLABE_ROOT}/sketches/Astrolabe/pm_mic.cpp"
	      "${ASTROLABE_ROOT}/sketches/Astrolabe/pm_fft.cpp"
	      "${ASTROLABE_ROOT}/lib/waveshare_board_audio/es8311.c"
	      "${ASTROLABE_ROOT}/lib/waveshare_board_audio/es7210.cpp"
    INCLUDE_DIRS
      "."
      "${ASTROLABE_ROOT}/sketches/Astrolabe"
      "${ASTROLABE_ROOT}/lib/waveshare_board_audio"
      "${ASTROLABE_ARDUINO_GFX_DIR}"
      "${ASTROLABE_JPEGDEC_DIR}"
    REQUIRES
      esp_wifi
      esp_netif
      esp_event
      esp_http_client
      esp_http_server
      mdns
      nvs_flash
      spiffs
      framework-arduinoespressif32)

set(BELL_DISABLE_MQTT ON CACHE BOOL "" FORCE)
set(BELL_DISABLE_SINKS ON CACHE BOOL "" FORCE)
set(BELL_DISABLE_CODECS ON CACHE BOOL "" FORCE)
set(BELL_SINK_ALSA OFF CACHE BOOL "" FORCE)
set(BELL_SINK_PORTAUDIO OFF CACHE BOOL "" FORCE)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
add_subdirectory("${ASTROLABE_ROOT}/third_party/cspot/cspot" "${CMAKE_CURRENT_BINARY_DIR}/cspot")

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/cspot/protobuf")
file(COPY "${ASTROLABE_ROOT}/cspot/astrolabe/generated/protobuf/"
     DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/cspot/protobuf")
target_include_directories(cspot PUBLIC "${ASTROLABE_ROOT}/cspot/astrolabe/generated")
target_include_directories(bell PUBLIC "${ASTROLABE_ROOT}/third_party/cspot/cspot/bell/external/libhelix-mp3")
target_compile_options(bell PRIVATE -Wno-error=overloaded-virtual)
add_dependencies(${COMPONENT_LIB} generate_proto_sources)
target_link_libraries(${COMPONENT_LIB} PUBLIC cspot)
target_include_directories(${COMPONENT_LIB} BEFORE PRIVATE
    "${ASTROLABE_ROOT}/third_party/cspot/cspot"
    "${ASTROLABE_ROOT}/third_party/cspot/cspot/bell/main/io/include"
    "${ASTROLABE_ROOT}/third_party/cspot/cspot/include"
    "${CMAKE_CURRENT_BINARY_DIR}/cspot"
    "${ASTROLABE_ROOT}/cspot/astrolabe/generated")
target_compile_options(${COMPONENT_LIB} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++20>)
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    ARDUINO_USB_CDC_ON_BOOT=1
    ARDUINO_USB_MODE=1
    BOARD_HAS_PSRAM)
