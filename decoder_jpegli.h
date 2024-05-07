#pragma once

#include "decoder_base.h"

class JpegliDecoder : public BaseDecoder {
private:

public:
  JpegliDecoder(std::vector<uint8_t> *data, bool rgb);
  ~JpegliDecoder() {};

  std::vector<uint8_t> decode() override;
  cmsHPROFILE get_color_profile() override { return nullptr; };
  std::string get_name() override { return "JPEGLI"; };
};
