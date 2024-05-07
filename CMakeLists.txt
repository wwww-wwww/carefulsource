cmake_minimum_required(VERSION 3.14)
project(carefulsource C CXX ASM)
include(FetchContent)

set(CMAKE_BUILD_TYPE Release)

add_library(carefulsource SHARED
  CarefulSource.cpp
  decoder_png.cpp
  decoder_jpeg.cpp
  decoder_jpegli.cpp
)

set_property(TARGET carefulsource PROPERTY CXX_STANDARD 20)

find_package(PkgConfig)

if(PkgConfig_FOUND)
  pkg_search_module(VAPOURSYNTH REQUIRED vapoursynth)
  install(TARGETS carefulsource DESTINATION ${VAPOURSYNTH_LIBDIR}/vapoursynth)
endif()

set(LCMS2_NAMES ${LCMS2_NAMES} lcms2 liblcms2 liblcms2_static)
find_library(lcms2 NAMES ${LCMS2_NAMES} REQUIRED)

find_package(PNG REQUIRED)
find_package(JPEG REQUIRED)

FetchContent_Declare(libjxl
  GIT_REPOSITORY https://github.com/libjxl/libjxl
  GIT_TAG v0.10.2
  BINARY_DIR build
  SUBBUILD_DIR subbuild
)

option(BUILD_TESTING "" OFF)
option(JPEGXL_ENABLE_DOXYGEN "" OFF)
option(JPEGXL_ENABLE_MANPAGES "" OFF)
option(JPEGXL_ENABLE_EXAMPLES "" OFF)
option(JPEGXL_ENABLE_SJPEG "" OFF)
option(JPEGXL_ENABLE_OPENEXR "" OFF)
option(JPEGXL_ENABLE_TRANSCODE_JPEG "" OFF)
option(JPEGXL_ENABLE_TOOLS "" OFF)
option(JPEGXL_ENABLE_BENCHMARK "" OFF)
option(JPEGXL_ENABLE_JPEGLI "" ON)
option(JPEGXL_ENABLE_JNI "" OFF)
option(JPEGXL_ENABLE_DEVTOOLS "" OFF)
option(JPEGXL_ENABLE_BENCHMARK "" OFF)

FetchContent_GetProperties(libjxl)

if(NOT libjxl_POPULATED)
  FetchContent_Populate(libjxl)
  add_subdirectory(${libjxl_SOURCE_DIR} ${libjxl_BINARY_DIR} EXCLUDE_FROM_ALL)
endif()

target_include_directories(carefulsource PRIVATE
  ${libjxl_BINARY_DIR}
  ${libjxl_SOURCE_DIR}
  $<TARGET_PROPERTY:hwy,INCLUDE_DIRECTORIES>
)

add_definitions(-DHAVE_JPEGLI)

target_link_libraries(carefulsource PRIVATE
  ${lcms2}
  PNG::PNG
  JPEG::JPEG
  jpegli-static
  jxl_tool
  jxl_threads

  # jxl_dec
  # jxl_threads
  # brotlidec
  # brotlienc
  # brotlicommon
  hwy
  -Wl,--allow-multiple-definition
)
