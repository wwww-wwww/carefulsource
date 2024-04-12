cmake_minimum_required(VERSION 3.14)
project(carefulsource C CXX ASM)

set(CMAKE_BUILD_TYPE Release)

add_library(carefulsource SHARED
  CarefulSource.cpp
  decoder_png.cpp
  decoder_jpeg.cpp
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
find_library(libjpeg NAMES libjpeg jpeg REQUIRED)

target_link_libraries(carefulsource PRIVATE ${lcms2} PNG::PNG ${libjxl} ${libjpeg})
