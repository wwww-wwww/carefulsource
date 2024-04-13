#pragma once

#include "decoder_base.h"
#include "jpeglib.h"

class JpegDecodeSession {
private:
  jpeg_error_mgr jerr = jpeg_error_mgr{};

  void get_color_profile();

public:
  jpeg_decompress_struct jinfo = jpeg_decompress_struct{};
  cmsHPROFILE src_profile = nullptr;
  bool finished_reading = false;

  JpegDecodeSession(std::vector<uint8_t> *data);
  ~JpegDecodeSession() { jpeg_destroy_decompress(&jinfo); };
};

class JpegDecoder : public BaseDecoder {
private:
  std::unique_ptr<JpegDecodeSession> d;
  bool subsampling_pad;
  bool jpeg_rgb;

public:
  JpegDecoder(std::vector<uint8_t> *data, bool subsampling_pad, bool jpeg_rgb);

  std::vector<uint8_t> decode() override;
  cmsHPROFILE get_color_profile() override { return d->src_profile; };
  std::string get_name() override { return "JPEG"; };

  static bool is_jpeg(uint8_t *data) {
    return data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF;
  };
};
