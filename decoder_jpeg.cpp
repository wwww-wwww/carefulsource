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
                         bool jpeg_rgb)
    : BaseDecoder(data), d(std::make_unique<JpegDecodeSession>(data)),
      subsampling_pad(subsampling_pad), jpeg_rgb(jpeg_rgb) {
  auto jcs = d->jinfo.jpeg_color_space;
  auto color = jcs == JCS_RGB                 ? VSColorFamily::cfRGB
               : jcs == JCS_YCbCr && jpeg_rgb ? VSColorFamily::cfRGB
               : jcs == JCS_YCbCr             ? VSColorFamily::cfYUV
               : jcs == JCS_GRAYSCALE         ? VSColorFamily::cfGray
               : jcs == JCS_CMYK              ? VSColorFamily::cfRGB
               : jcs == JCS_YCCK              ? VSColorFamily::cfRGB
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

  dinfo->do_fancy_upsampling = true;
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
    int scalingfactor = 1;
    int i, retval = 0;
    int pw[3], ph[3], iw[3], tmpbufsize = 0, usetmpbuf = 0, th[3];
    JSAMPLE *_tmpbuf = NULL, *ptr;
    JSAMPROW *outbuf[3], *tmpbuf[3];

    for (i = 0; i < 3; i++) {
      tmpbuf[i] = NULL;
      outbuf[i] = NULL;
    }

    // dinfo->scale_num = scalingfactor;
    // dinfo->scale_denom = scalingfactor;
    // jpeg_calc_output_dimensions(d->jinfo);

    int dctsize = DCTSIZE * scalingfactor;

    /*for (i = 0; i < dinfo->num_components; i++) {
      jpeg_component_info *compptr = &dinfo->comp_info[i];
      int ih;

      iw[i] = compptr->width_in_blocks * dctsize;
      ih = compptr->height_in_blocks * dctsize;
      pw[i] = i == 0 ? dinfo->output_width : dinfo->output_width /
    (info.subsampling_w << 1); ph[i] = i == 0 ? dinfo->output_height :
    dinfo->output_height / (info.subsampling_h << 1); if (iw[i] != pw[i] ||
    ih
    != ph[i]) usetmpbuf = 1; th[i] = compptr->v_samp_factor * dctsize;
      tmpbufsize += iw[i] * th[i];

     outbuf[i] = (JSAMPROW *)malloc(sizeof(JSAMPROW) * ph[i]);

      ptr = dstPlanes[i];
      for (row = 0; row < ph[i]; row++) {
        outbuf[i][row] = ptr;
        ptr += (strides && strides[i] != 0) ? strides[i] : pw[i];
      }
    }*/
    /*if (usetmpbuf) {
      _tmpbuf = (JSAMPLE *)malloc(sizeof(JSAMPLE) * tmpbufsize);

      ptr = _tmpbuf;
      for (i = 0; i < dinfo->num_components; i++) {
        (tmpbuf[i] = (JSAMPROW *)malloc(sizeof(JSAMPROW) * th[i]);
        for (row = 0; row < th[i]; row++) {
          tmpbuf[i][row] = ptr; ptr += iw[i];
        }
      }
    }*/

    dinfo->raw_data_out = TRUE;

    jpeg_start_decompress(dinfo);
    for (int row = 0; row < (int)dinfo->output_height;
         row += dinfo->output_height) {
      JSAMPARRAY yuvptr[3];
      int crow[3];

      for (i = 0; i < dinfo->num_components; i++) {
        jpeg_component_info *compptr = &dinfo->comp_info[i];

        if (info.subsampling_h == 1 && info.subsampling_w == 1) {
          // compptr->_DCT_scaled_size = dctsize;
          // compptr->MCU_sample_width = 16 * scalingfactor *
          //                             compptr->v_samp_factor /
          //                             dinfo->max_v_samp_factor;
          // dinfo->idct->inverse_DCT[i] = dinfo->idct->inverse_DCT[0];
        }
        crow[i] = row * compptr->v_samp_factor / dinfo->max_v_samp_factor;
        if (usetmpbuf)
          yuvptr[i] = tmpbuf[i];
        else
          yuvptr[i] = &outbuf[i][crow[i]];
      }
      jpeg_read_raw_data(dinfo, yuvptr, 20);
      /*if (usetmpbuf) {
        int j;

        for (i = 0; i < dinfo->num_components; i++) {
          for (j = 0; j < MIN(th[i], ph[i] - crow[i]); j++) {
            memcpy(outbuf[i][crow[i] + j], tmpbuf[i][j], pw[i]);
          }
        }
      }*/
    }
    jpeg_finish_decompress(dinfo);
  } else {
  }

  d->finished_reading = true;

  return pixels;
}
