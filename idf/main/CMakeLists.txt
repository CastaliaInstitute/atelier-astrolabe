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
    "${ASTROLABE_ARDUINO_LIBDEPS}/XPowersLib/src")

file(GLOB_RECURSE ASTROLABE_LIB_CPP
     "${ASTROLABE_ARDUINO_LIBDEPS}/GFX_Library_for_Arduino/src/*.cpp"
     "${ASTROLABE_ARDUINO_LIBDEPS}/JPEGDEC/src/*.cpp"
     "${ASTROLABE_ARDUINO_LIBDEPS}/PNGdec/src/*.cpp"
     "${ASTROLABE_ARDUINO_LIBDEPS}/SensorLib/src/*.cpp"
     "${ASTROLABE_ARDUINO_LIBDEPS}/XPowersLib/src/*.cpp")
# Watch display uses Arduino_ESP32QSPI; skip legacy SPI/RGB databus (arduino-esp32 3.3 API drift).
list(FILTER ASTROLABE_LIB_CPP EXCLUDE REGEX ".*/databus/Arduino_ESP32RGBPanel\\.cpp$")
list(FILTER ASTROLABE_LIB_CPP EXCLUDE REGEX ".*/databus/Arduino_ESP32SPI\\.cpp$")
list(FILTER ASTROLABE_LIB_CPP EXCLUDE REGEX ".*/databus/Arduino_ESP32SPIDMA\\.cpp$")
file(GLOB_RECURSE ASTROLABE_LIB_C
     "${ASTROLABE_ARDUINO_LIBDEPS}/GFX_Library_for_Arduino/src/*.c"
     "${ASTROLABE_ARDUINO_LIBDEPS}/JPEGDEC/src/*.c"
     "${ASTROLABE_ARDUINO_LIBDEPS}/PNGdec/src/*.c"
     "${ASTROLABE_ARDUINO_LIBDEPS}/SensorLib/src/*.c"
     "${ASTROLABE_ARDUINO_LIBDEPS}/XPowersLib/src/*.c")
file(GLOB_RECURSE ASTROLABE_LIB_ASM
     "${ASTROLABE_ARDUINO_LIBDEPS}/JPEGDEC/src/*.S"
     "${ASTROLABE_ARDUINO_LIBDEPS}/PNGdec/src/*.S")

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
      espressif__esp-dsp
      bt
      esp_lcd
      astrolabe_audio_pipeline
      astrolabe_time
      minimp3
      waveshare_board_audio)

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    BOARD_HAS_PSRAM
    ASTROLABE_UAC_DISABLED=1
    CONFIG_TINYUSB_AUDIO_ENABLED=0
    CONFIG_UAC_SPEAKER_CHANNEL_NUM=0
    CONFIG_UAC_MIC_CHANNEL_NUM=0
    CONFIG_ARDUINO_LOOP_STACK_SIZE=24576
    U8G2_USE_LARGE_FONTS)

target_compile_options(${COMPONENT_LIB} PRIVATE
    "-Wno-error=format-truncation"
    "$<$<COMPILE_LANGUAGE:C>:-include${CMAKE_CURRENT_LIST_DIR}/astrolabe_idf_compat.h>"
    "$<$<COMPILE_LANGUAGE:CXX>:-include${CMAKE_CURRENT_LIST_DIR}/astrolabe_idf_compat.h>"
    "-DU8G2_FONT_SECTION(name)="
    "-DU8X8_FONT_SECTION(name)=")
