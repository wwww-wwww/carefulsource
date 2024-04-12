#include "decoder_jpeg.h"

JpegDecodeSession::JpegDecodeSession(std::vector<uint8_t> *data) {
  jinfo = jpeg_decompress_struct{};
  jerr = jpeg_error_mgr{};
  jinfo.err = jpeg_std_error(&jerr);
  int rc;

  jerr.error_exit = [](j_common_ptr info) {
    char jpegLastErrorMsg[JMSG_LENGTH_MAX];
    (*(info->err->format_message))(info, jpegLastErrorMsg);
    throw std::runtime_error(jpegLastErrorMsg);
  };

  jpeg_create_decompress(&jinfo);
  jpeg_mem_src(&jinfo, data->data(), data->size());
  jpeg_save_markers(&jinfo, JPEG_APP0 + 2, 0xFFFF);
  rc = jpeg_read_header(&jinfo, TRUE);

  if (rc != 1) {
    throw std::runtime_error("Invalid JPEG");
  }

  jpeg_start_decompress(&jinfo);

  finished_reading = false;
}

JpegDecoder::JpegDecoder(std::vector<uint8_t> *data)
    : BaseDecoder(data), d(std::make_unique<JpegDecodeSession>(data)) {

  info = {
      .width = d->jinfo.output_width,
      .height = d->jinfo.output_height,
      .components = static_cast<uint32_t>(d->jinfo.output_components),
      .has_alpha = false,
      .color = VSColorFamily::cfRGB,
      .sample_type = VSSampleType::stInteger,
      .bits = 8,
      .subsampling_w = 0,
      .subsampling_h = 0,
  };
}

std::vector<uint8_t> JpegDecoder::decode() {
  if (d->finished_reading)
    d = std::make_unique<JpegDecodeSession>(m_data);

  std::vector<uint8_t> pixels;
  return pixels;
}

cmsHPROFILE JpegDecoder::get_color_profile() { return nullptr; }
