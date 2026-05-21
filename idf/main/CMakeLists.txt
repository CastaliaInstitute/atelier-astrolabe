set(ASTROLABE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
set(ASTROLABE_SKETCH_DIR "${ASTROLABE_ROOT}/sketches/Astrolabe")

file(GLOB_RECURSE ASTROLABE_CPP
     "${ASTROLABE_SKETCH_DIR}/*.cpp")
file(GLOB_RECURSE ASTROLABE_C
     "${ASTROLABE_SKETCH_DIR}/*.c")

list(FILTER ASTROLABE_CPP EXCLUDE REGEX ".*/third_party/tinyusb/.*")
list(FILTER ASTROLABE_C EXCLUDE REGEX ".*/third_party/tinyusb/.*")
list(FILTER ASTROLABE_CPP EXCLUDE REGEX ".*\\.ino\\.cpp$")
list(FILTER ASTROLABE_C EXCLUDE REGEX ".*/pm_tinyusb_unused_stubs\\.c$")
list(FILTER ASTROLABE_C EXCLUDE REGEX ".*/pm_usb_uac_component\\.c$")

set(ASTROLABE_ARDUINO_LIBDEPS "" CACHE PATH
    "PlatformIO libdeps directory containing Arduino_GFX, SensorLib, XPowersLib, JPEGDEC, PNGdec, and ArduinoJson")
if(NOT ASTROLABE_ARDUINO_LIBDEPS)
  foreach(candidate
      "${ASTROLABE_ROOT}/.pio/libdeps/waveshare_s3_175"
      "${ASTROLABE_ROOT}/.pio/libdeps/waveshare_s3_175_qemu")
    if(EXISTS "${candidate}")
      set(ASTROLABE_ARDUINO_LIBDEPS "${candidate}")
      break()
    endif()
  endforeach()
endif()

if(NOT EXISTS "${ASTROLABE_ARDUINO_LIBDEPS}")
  message(FATAL_ERROR
      "Missing Arduino library deps. Run `pio pkg install -e waveshare_s3_175`, "
      "or configure with -DASTROLABE_ARDUINO_LIBDEPS=/path/to/.pio/libdeps/waveshare_s3_175.")
endif()

set(ASTROLABE_ARDUINO_LIB_INCLUDES
    "${ASTROLABE_ARDUINO_LIBDEPS}/ArduinoJson/src"
    "${ASTROLABE_ARDUINO_LIBDEPS}/GFX_Library_for_Arduino/src"
    "${ASTROLABE_ARDUINO_LIBDEPS}/JPEGDEC/src"
    "${ASTROLABE_ARDUINO_LIBDEPS}/PNGdec/src"
    "${ASTROLABE_ARDUINO_LIBDEPS}/SensorLib/src"
    "${ASTROLABE_ARDUINO_LIBDEPS}/XPowersLib/src"
    "${ASTROLABE_ROOT}/idf/managed_components/espressif__esp-dsp/modules/fft/include")

file(GLOB_RECURSE ASTROLABE_LIB_CPP
     "${ASTROLABE_ARDUINO_LIBDEPS}/GFX_Library_for_Arduino/src/*.cpp"
     "${ASTROLABE_ARDUINO_LIBDEPS}/JPEGDEC/src/*.cpp"
     "${ASTROLABE_ARDUINO_LIBDEPS}/PNGdec/src/*.cpp"
     "${ASTROLABE_ARDUINO_LIBDEPS}/SensorLib/src/*.cpp"
     "${ASTROLABE_ARDUINO_LIBDEPS}/XPowersLib/src/*.cpp")
file(GLOB_RECURSE ASTROLABE_LIB_C
     "${ASTROLABE_ARDUINO_LIBDEPS}/GFX_Library_for_Arduino/src/*.c"
     "${ASTROLABE_ARDUINO_LIBDEPS}/JPEGDEC/src/*.c"
     "${ASTROLABE_ARDUINO_LIBDEPS}/PNGdec/src/*.c"
     "${ASTROLABE_ARDUINO_LIBDEPS}/SensorLib/src/*.c"
     "${ASTROLABE_ARDUINO_LIBDEPS}/XPowersLib/src/*.c")
file(GLOB_RECURSE ASTROLABE_LIB_ASM
     "${ASTROLABE_ARDUINO_LIBDEPS}/JPEGDEC/src/*.S"
     "${ASTROLABE_ARDUINO_LIBDEPS}/PNGdec/src/*.S")

set(ASTROLABE_GFX_QSPI "${ASTROLABE_ARDUINO_LIBDEPS}/GFX_Library_for_Arduino/src/databus/Arduino_ESP32QSPI.cpp")
list(FILTER ASTROLABE_LIB_CPP EXCLUDE REGEX ".*/GFX_Library_for_Arduino/src/databus/.*SPI.*\\.cpp$")
list(APPEND ASTROLABE_LIB_CPP "${ASTROLABE_GFX_QSPI}")
list(FILTER ASTROLABE_LIB_CPP EXCLUDE REGEX ".*/GFX_Library_for_Arduino/src/databus/Arduino_ESP32RGBPanel\\.cpp$")

idf_component_register(
    SRCS
      "astrolabe_arduino_entry.cpp"
      "astrolabe_idf_compat.cpp"
      ${ASTROLABE_CPP}
      ${ASTROLABE_C}
      ${ASTROLABE_LIB_CPP}
      ${ASTROLABE_LIB_C}
      ${ASTROLABE_LIB_ASM}
    INCLUDE_DIRS
      "${ASTROLABE_SKETCH_DIR}"
      "${ASTROLABE_SKETCH_DIR}/faces"
      "${ASTROLABE_ROOT}/include"
      "${ASTROLABE_ROOT}/u8g2_shim"
      ${ASTROLABE_ARDUINO_LIB_INCLUDES}
    REQUIRES
      espressif__arduino-esp32
      bt
      esp_lcd
      minimp3
      waveshare_board_audio)

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    ARDUINO_USB_CDC_ON_BOOT=1
    ARDUINO_USB_MODE=1
    BOARD_HAS_PSRAM
    ASTROLABE_UAC_DISABLED=1
    CONFIG_TINYUSB_AUDIO_ENABLED=0
    CONFIG_UAC_SPEAKER_CHANNEL_NUM=0
    CONFIG_UAC_MIC_CHANNEL_NUM=0
    CONFIG_ARDUINO_LOOP_STACK_SIZE=24576
    U8G2_USE_LARGE_FONTS)

idf_component_get_property(ASTROLABE_ARDUINO_LIB espressif__arduino-esp32 COMPONENT_LIB)
target_compile_definitions(${ASTROLABE_ARDUINO_LIB} PUBLIC
    ARDUINO_USB_CDC_ON_BOOT=1
    ARDUINO_USB_MODE=1)
target_link_libraries(${ASTROLABE_ARDUINO_LIB} INTERFACE
    "-u _Z8ssl_initP17sslclient_context"
    "-u HWCDCSerial")

target_compile_options(${COMPONENT_LIB} PRIVATE
    "-Wno-error=format-truncation"
    "$<$<COMPILE_LANGUAGE:C>:-include${CMAKE_CURRENT_LIST_DIR}/astrolabe_idf_compat.h>"
    "$<$<COMPILE_LANGUAGE:CXX>:-include${CMAKE_CURRENT_LIST_DIR}/astrolabe_idf_compat.h>"
    "-DU8G2_FONT_SECTION(name)="
    "-DU8X8_FONT_SECTION(name)=")
