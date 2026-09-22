#pragma once

#include "decoder_base.h"
#include "decoder_factory.h"

#include "VSHelper4.h"
#include "VapourSynth4.h"
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

using imagedecoder::BaseDecoder;
using imagedecoder::ColorFamily;
using imagedecoder::create_decoder;
using imagedecoder::DecodeOptions;
using imagedecoder::DecodeProgress;
using imagedecoder::ImageInfo;
using imagedecoder::OutputMode;
using imagedecoder::padded_height;
using imagedecoder::padded_width;
using imagedecoder::SNIFF_BYTES;

static inline VSColorFamily vs_color(ColorFamily c) {
  return static_cast<VSColorFamily>(c);
}

static inline VSSampleType vs_sample_type(imagedecoder::SampleType t) {
  return static_cast<VSSampleType>(t);
}

struct ImageSourceData final {
  std::unique_ptr<BaseDecoder> decoder;
  imagedecoder::DecodeOptions options;
  ImageInfo info;
  VSVideoInfo vi;

  std::vector<uint8_t> profile_bytes;
  std::string format_name;

  // Guards the decoder and cache; pixels are copied out without it.
  std::mutex lock;
  uint32_t produced = 0;
  bool started = false;
  // Recent frames, so a slightly out-of-order request needs no restart.
  struct Cached {
    uint32_t index;
    std::shared_ptr<const std::vector<uint8_t>> pixels;
    uint32_t duration_ms;
  };
  std::vector<Cached> recent;
  imagedecoder::HdrKind hdr_kind = imagedecoder::HdrKind::None;
  int hdr_primaries = 1;
};

struct DecodedFrame final {
  std::shared_ptr<const std::vector<uint8_t>> pixels;
  uint32_t duration_ms = 0;
};

struct ConvertColorData final {
  VSNode *node = nullptr;
  const VSVideoInfo *src_vi = nullptr;
  VSVideoInfo vi = {};
  std::string target;
  cmsHPROFILE target_profile = nullptr;
  cmsHPROFILE input_profile = nullptr;
  bool float_output = false;

  std::vector<uint8_t> target_profile_bytes;
  bool input_xyz = false;

  // By profile bytes; shared_ptr so eviction can't free one in use.
  struct Transforms {
    std::shared_ptr<void> to_target;
    std::shared_ptr<void> narrow;
  };
  std::map<std::string, Transforms> transforms;
  std::vector<std::string> transform_order;

  // Guards the cache and the shared target profile.
  std::mutex lock;
};
