#pragma once

#include "VapourSynth4.h"
#include "lcms2.h"
#include <memory>
#include <stdint.h>
#include <string>
#include <vector>

struct ImageInfo final {
  uint32_t width;
  uint32_t height;
  uint32_t actual_width = 0;
  uint32_t actual_height = 0;
  uint32_t components;
  bool has_alpha = false;
  VSColorFamily color;
  VSSampleType sample_type;
  uint32_t bits;
  uint32_t subsampling_w = 0;
  uint32_t subsampling_h = 0;
  int yuv_matrix = 1;
};

class BaseDecoder {
public:
  BaseDecoder() = delete;
  BaseDecoder(std::vector<uint8_t> *data) : m_data(data){};
  virtual ~BaseDecoder() = default;

  ImageInfo info;
  std::vector<uint8_t> *m_data;

  virtual std::vector<uint8_t> decode() = 0;
  virtual cmsHPROFILE get_color_profile() = 0;
  virtual std::string get_name() = 0;
};
