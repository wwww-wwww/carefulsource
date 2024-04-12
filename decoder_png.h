#pragma once

#include "decoder_base.h"
#include "png.h"

class PngDecodeSession {
private:
  bool _get_color_profile();

public:
  png_struct *png;
  png_info *pinfo;

  std::vector<uint8_t> *m_data;
  uint32_t m_read;
  uint32_t m_remain;

  cmsHPROFILE src_profile;
  bool finished_reading = false;

  PngDecodeSession(std::vector<uint8_t> *data);
  ~PngDecodeSession() {
    if (src_profile) {
      cmsCloseProfile(src_profile);
    }

    png_destroy_read_struct(&png, &pinfo, NULL);
  }
};

class PngDecoder : public BaseDecoder {
private:
  std::unique_ptr<PngDecodeSession> d;

public:
  PngDecoder(std::vector<uint8_t> *data);

  std::vector<uint8_t> decode() override;
  cmsHPROFILE get_color_profile() override { return d->src_profile; };
  std::string get_name() override { return "PNG"; };

  static bool is_png(uint8_t *data) {
    return data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' &&
           data[3] == 'G';
  };
};
