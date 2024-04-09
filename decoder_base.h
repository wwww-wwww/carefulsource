#pragma once

#include <string>
#include <vector>

struct ImageInfo final {
  uint32_t width;
  uint32_t height;
};

class BaseDecoder {
private:
  std::string path;

public:
  BaseDecoder(std::string path) : path(path){};
  virtual std::vector<uint8_t> decode() = 0;
  ImageInfo info;
};
