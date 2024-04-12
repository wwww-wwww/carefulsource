#pragma once

#include "decoder_base.h"
#include "jpeglib.h"

class JpegDecodeSession {
private:
  jpeg_error_mgr jerr;

public:
  jpeg_decompress_struct jinfo;
  bool finished_reading = false;

  JpegDecodeSession(std::vector<uint8_t> *data);
  ~JpegDecodeSession() { jpeg_destroy_decompress(&jinfo); };
};

class JpegDecoder : public BaseDecoder {
private:
  std::unique_ptr<JpegDecodeSession> d;

public:
  JpegDecoder(std::vector<uint8_t> *data);

  std::vector<uint8_t> decode() override;
  cmsHPROFILE get_color_profile() override;
  std::string get_name() override { return "JPEG"; };

  static bool is_jpeg(uint8_t *data) {
    return data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF;
  };
};
