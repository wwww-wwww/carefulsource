#pragma once

#include "Vapoursynth4.h"
#include <string>
#include <vector>

struct ImageInfo final {
  uint32_t width;
  uint32_t height;
  uint32_t components;
  bool has_alpha;
  VSColorFamily color;
  VSSampleType sample_type;
  uint32_t bits;
  uint32_t subsampling_w;
  uint32_t subsampling_h;
};

class BaseDecoder {
private:
  std::string path;

public:
  BaseDecoder(std::string path) : path(path){};
  virtual std::vector<uint8_t> decode() = 0;
  ImageInfo info;
};
