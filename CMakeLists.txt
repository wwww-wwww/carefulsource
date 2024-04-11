cmake_minimum_required(VERSION 3.14)
project(carefulsource C CXX ASM)
include(FetchContent)

set(CMAKE_BUILD_TYPE Release)

add_library(carefulsource SHARED
  CarefulSource.cpp
  decoder_png.cpp
)

set_property(TARGET carefulsource PROPERTY CXX_STANDARD 20)

set(LCMS2_NAMES ${LCMS2_NAMES} lcms2 liblcms2 liblcms2_static)
find_library(lcms2 NAMES ${LCMS2_NAMES})

find_package(PNG REQUIRED)

target_link_libraries(carefulsource PRIVATE ${lcms2} PNG::PNG)
