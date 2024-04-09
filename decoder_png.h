#pragma once

#include "decoder_base.h"
#include "png.h"
#include <fstream>

class PngDecoder : public BaseDecoder {
private:
  struct PngReader {
    std::ifstream file;
    uint32_t size;
    uint32_t remain;
  };

  png_struct *png;
  png_info *pinfo;
  PngReader reader;

  bool finished_reading = false;

public:
  PngDecoder(std::string path);
  void initialize();

  std::vector<uint8_t> decode() override;
  cmsHPROFILE get_color_profile() override;
};
