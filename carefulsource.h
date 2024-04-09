#pragma once

#include "decoder_base.h"

#include <VSHelper4.h>
#include <VapourSynth4.h>
#include <memory>

struct ImageSourceData final {
  VSNode *node;
  VSVideoInfo vi;
  VSVideoFormat format;
  std::unique_ptr<BaseDecoder> decoder;
};
