#pragma once

#include "decoder_base.h"

#include "VSHelper4.h"
#include "VapourSynth4.h"
#include <memory>
#include <string>

struct ImageSourceData final {
  std::vector<uint8_t> data;
  std::unique_ptr<BaseDecoder> decoder;
  VSVideoInfo vi;
};

struct ConvertColorData final {
  VSNode *node;
  const VSVideoInfo *src_vi;
  VSVideoInfo vi;
  std::string target;
  cmsHPROFILE target_profile;
  cmsHPROFILE input_profile = nullptr;
  bool float_output;
};
