#pragma once

#include "decoder_base.h"

#include <VSHelper4.h>
#include <VapourSynth4.h>
#include <memory>
#include <string>

struct ImageSourceData final {
  std::unique_ptr<BaseDecoder> decoder;
  VSVideoInfo vi;
};

struct ConvertColorData final {
  VSNode *node;
  const VSVideoInfo *src_vi;
  VSVideoInfo vi;
  std::string target;
};
