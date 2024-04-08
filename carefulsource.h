#pragma once

#include <VSHelper4.h>
#include <VapourSynth4.h>

#include <vector>
#include <string>

#include "png.h"

struct PngReader {
  uint8_t *bytes;
  uint32_t read;
  uint32_t remain;
};

struct ImageSourceData final {
  VSNode *node;
  VSVideoInfo vi;
  VSVideoFormat format;
  std::string file_path;
};
