#include "decoder_jpeg.h"
#include <iostream>

JpegDecodeSession::JpegDecodeSession(std::vector<uint8_t> *data) {
  jinfo.err = jpeg_std_error(&jerr);
  int rc;

  jerr.error_exit = [](j_common_ptr info) {
    char jpegLastErrorMsg[JMSG_LENGTH_MAX];
    (*(info->err->format_message))(info, jpegLastErrorMsg);
    throw std::runtime_error(jpegLastErrorMsg);
  };

  jpeg_create_decompress(&jinfo);
  jpeg_mem_src(&jinfo, data->data(), (uint32_t)data->size());
  jpeg_save_markers(&jinfo, JPEG_APP0 + 2, 0xFFFF);
  rc = jpeg_read_header(&jinfo, true);

  if (rc != 1) {
    throw std::runtime_error("Invalid JPEG");
  }

  get_color_profile();

  finished_reading = false;
}

void JpegDecodeSession::get_color_profile() {
  JOCTET *icc_data;
  unsigned int icc_size;
  if (!jpeg_read_icc_profile(&jinfo, &icc_data, &icc_size)) {
    return;
  }
  src_profile = cmsOpenProfileFromMem(icc_data, icc_size);
  free(icc_data);

  cmsColorSpaceSignature profileSpace = cmsGetColorSpace(src_profile);

  auto colorspace = jinfo.jpeg_color_space;

  if ((colorspace == JCS_GRAYSCALE && profileSpace != cmsSigGrayData) ||
      (colorspace == JCS_CMYK && profileSpace != cmsSigCmykData) ||
      (colorspace == JCS_YCCK && profileSpace != cmsSigCmykData) ||
      (colorspace == JCS_RGB && profileSpace != cmsSigRgbData) ||
      (colorspace == JCS_YCbCr && profileSpace != cmsSigRgbData)) {
    cmsCloseProfile(src_profile);
    src_profile = nullptr;
  }
}

JpegDecoder::JpegDecoder(std::vector<uint8_t> *data, bool subsampling_pad,
                         bool rgb, bool fancy_upsampling)
    : BaseDecoder(data), d(std::make_unique<JpegDecodeSession>(data)),
      subsampling_pad(subsampling_pad), rgb(rgb),
      fancy_upsampling(fancy_upsampling) {
  auto jcs = d->jinfo.jpeg_color_space;
  auto color = jcs == JCS_RGB            ? VSColorFamily::cfRGB
               : jcs == JCS_YCbCr && rgb ? VSColorFamily::cfRGB
               : jcs == JCS_YCbCr        ? VSColorFamily::cfYUV
               : jcs == JCS_GRAYSCALE    ? VSColorFamily::cfGray
               : jcs == JCS_CMYK         ? VSColorFamily::cfRGB
               : jcs == JCS_YCCK         ? VSColorFamily::cfRGB
                                         : VSColorFamily::cfUndefined;

  uint32_t subsampling_w = 0;
  uint32_t subsampling_h = 0;

  uint32_t actual_width = static_cast<uint32_t>(d->jinfo.image_width);
  uint32_t actual_height = static_cast<uint32_t>(d->jinfo.image_height);

  uint32_t width = actual_width;
  uint32_t height = actual_height;

  if (color == VSColorFamily::cfYUV) {
    subsampling_w = d->jinfo.comp_info[0].h_samp_factor >> 1;
    subsampling_h = d->jinfo.comp_info[0].v_samp_factor >> 1;

    if (subsampling_w > 0) {
      uint8_t subsamp_size = 1 << subsampling_w;
      if (width % subsamp_size != 0) {
        std::cout << width << " % " << subsamp_size << " != 0" << std::endl;
        width = width + subsamp_size - (width % subsamp_size);
      }
    }

    if (subsampling_h > 0) {
      uint8_t subsamp_size = 1 << subsampling_h;
      if (height % subsamp_size != 0) {
        std::cout << height << " % " << subsamp_size << " != 0" << std::endl;
        height = height + subsamp_size - (height % subsamp_size);
      }
    }
  }

  // TODO?: cmyk using alpha

  info = {
      .width = width,
      .height = height,
      .actual_width = actual_width,
      .actual_height = actual_height,
      .components = static_cast<uint32_t>(d->jinfo.num_components),
      .color = color,
      .sample_type = VSSampleType::stInteger,
      .bits = 8,
      .subsampling_w = subsampling_w,
      .subsampling_h = subsampling_h,
      .yuv_matrix = 5,
  };
}

std::vector<uint8_t> JpegDecoder::decode() {
  if (d->finished_reading)
    d = std::make_unique<JpegDecodeSession>(m_data);

  std::vector<uint8_t> pixels(info.height * info.width * info.components *
                              (info.bits == 8 ? 1 : 2));
  std::fill(pixels.begin(), pixels.end(), 0);

  auto *dinfo = &d->jinfo;

  auto jcs = dinfo->jpeg_color_space;

  dinfo->out_color_space = jcs == JCS_YCCK                       ? JCS_CMYK
                           : jcs == JCS_CMYK                     ? JCS_CMYK
                           : info.color == VSColorFamily::cfYUV  ? JCS_YCbCr
                           : info.color == VSColorFamily::cfGray ? JCS_GRAYSCALE
                                                                 : JCS_RGB;

  if (jcs == JCS_CMYK || jcs == JCS_YCCK) {
    cmsUInt32Number in_type;
    if (dinfo->saw_Adobe_marker) {
      in_type = TYPE_CMYK_8_REV;
    } else {
      in_type = TYPE_CMYK_8;
    }
    // TODO: transform into rgb
  }

  dinfo->do_fancy_upsampling = fancy_upsampling;
  dinfo->dct_method = JDCT_ISLOW;

  uint32_t stride = info.width * info.components;

  if (info.subsampling_w == 0 && info.subsampling_h == 0) {
    jpeg_start_decompress(dinfo);
    for (uint32_t y = 0; y < dinfo->output_height; y++) {
      uint8_t *row_ptr = pixels.data() + stride * y;
      jpeg_read_scanlines(dinfo, &row_ptr, 1);
    }
    jpeg_finish_decompress(dinfo);
  } else if (info.color == VSColorFamily::cfYUV) {
    dinfo->raw_data_out = true;

    jpeg_start_decompress(dinfo);

    int ph[3];
    int pw[3];
    int ih[3];
    int iw[3];

    int pwidth = dinfo->output_width;
    if (pwidth % 8 != 0)
      pwidth += (8 - (dinfo->output_width % 8));

    int pheight = dinfo->output_width;
    if (pheight % 8 != 0)
      pheight += (8 - (dinfo->output_width % 8));

    uint8_t *ptr = pixels.data();

    std::vector<JSAMPROW *> outbuf[3];

    for (int i = 0; i < dinfo->num_components; i++) {
      jpeg_component_info *compptr = &dinfo->comp_info[i];
      ph[i] = pheight * compptr->v_samp_factor / dinfo->max_v_samp_factor;
      pw[i] = pwidth * compptr->h_samp_factor / dinfo->max_h_samp_factor;
      ih[i] = info.height * compptr->v_samp_factor / dinfo->max_v_samp_factor;
      iw[i] = info.width * compptr->h_samp_factor / dinfo->max_h_samp_factor;
      outbuf[i].resize(ph[i] * pw[i]);

      for (int row = 0; row < ph[i]; row++) {
        outbuf[i].data()[row] = (JSAMPROW *)(ptr + iw[i] * row);
      }
      ptr += iw[i] * ph[i];
    }

    for (int row = 0; row < (int)dinfo->output_height;
         row += dinfo->max_v_samp_factor * dinfo->min_DCT_scaled_size) {
      JSAMPARRAY yuvptr[3];
      for (int i = 0; i < dinfo->num_components; i++) {
        jpeg_component_info *compptr = &dinfo->comp_info[i];
        yuvptr[i] =
            &((JSAMPROW *)outbuf[i].data())[row * compptr->v_samp_factor /
                                            dinfo->max_v_samp_factor];
      }

      jpeg_read_raw_data(dinfo, yuvptr,
                         dinfo->max_v_samp_factor * dinfo->min_DCT_scaled_size);
    }
    jpeg_finish_decompress(dinfo);
  } else {
    throw std::runtime_error("huh?");
  }

  d->finished_reading = true;

  return pixels;
}
