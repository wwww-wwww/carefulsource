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
  cmsHPROFILE src_profile;
  bool _get_color_profile();

public:
  PngDecoder(std::string path);
  ~PngDecoder() {
    if (src_profile) {
      cmsCloseProfile(src_profile);
    }
  };
  void initialize();

  std::vector<uint8_t> decode() override;
  cmsHPROFILE get_color_profile() override { return src_profile; };
};
