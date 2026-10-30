# Read only by the ESP-IDF based environments (*_tls in platformio.ini): PlatformIO leaves
# build_src_filter out of IDF builds, so the files of one board are picked here, the same ones
# build_src_filter picks for the board's Arduino environment. The board comes from
# board_build.cmake_extra_args = -DBOARD=<folder in src/boards>; the environment is named
# <BOARD>_tls. The Arduino environments do not use this file.

# IDF first reads this file in a script-mode pass (to learn the requirements) that does not get
# the -D options; only the real configuration needs the board
if(NOT DEFINED BOARD)
  if(CMAKE_SCRIPT_MODE_FILE)
    set(BOARD jc3248w535)
  else()
    message(FATAL_ERROR "set -DBOARD=<folder in src/boards> (board_build.cmake_extra_args)")
  endif()
endif()

set(S ${CMAKE_SOURCE_DIR}/src)
set(globs
  ${S}/app/*.c*
  ${S}/drivers/common/*.c*
  ${S}/drivers/esp32/*.c*
  ${S}/platform/common/*.c*
  ${S}/platform/esp32/*.c*
  ${S}/boards/${BOARD}/*.c*)
if(BOARD STREQUAL "jc3248w535")  # the QSPI panel's driver is S3 only
  list(APPEND globs ${S}/drivers/esp32s3/*.c*)
endif()
file(GLOB_RECURSE app_sources ${globs})

idf_component_register(SRCS ${app_sources} INCLUDE_DIRS .)

# build_flags of the environments, and ArduinoJson from PlatformIO's library folder
target_compile_options(${COMPONENT_LIB} PRIVATE
  $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++17> -O2 -DPERF_PROBE=1 -DBOARD_HAS_PSRAM)
if(NOT BOARD STREQUAL "s3lcd13")  # its USB goes through a UART bridge, the others have native USB
  target_compile_options(${COMPONENT_LIB} PRIVATE -DARDUINO_USB_CDC_ON_BOOT=1)
endif()
target_include_directories(${COMPONENT_LIB} PRIVATE
  ${CMAKE_SOURCE_DIR}/.pio/libdeps/${BOARD}_tls/ArduinoJson/src)
