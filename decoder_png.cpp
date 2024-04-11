#include "decoder_png.h"

#include "lcms2.h"
#include <algorithm>
#include <iostream>

PngDecoder::PngDecoder(std::string path) : BaseDecoder(path) {
  reader.file.open(path, std::ios_base::binary);
  reader.file.unsetf(std::ios::skipws);
  reader.file.seekg(0, std::ios::end);
  reader.size = reader.file.tellg();

  initialize();

  auto color_type = png_get_color_type(png, pinfo);

  info = {
      .width = png_get_image_width(png, pinfo),
      .height = png_get_image_height(png, pinfo),
      .components = png_get_channels(png, pinfo),
      .has_alpha = (bool)(color_type & PNG_COLOR_MASK_ALPHA),
      .color = color_type & PNG_COLOR_MASK_COLOR ? VSColorFamily::cfRGB
                                                 : VSColorFamily::cfGray,
      .sample_type = VSSampleType::stInteger,
      .bits = png_get_bit_depth(png, pinfo),
      .subsampling_w = 0,
      .subsampling_h = 0,
  };
}

static void PNGDoGammaCorrection(png_structp png, png_infop pinfo) {
  if (!png_get_valid(png, pinfo, PNG_INFO_gAMA))
    return;

  double aGamma;

  if (png_get_gAMA(png, pinfo, &aGamma)) {
    if ((aGamma <= 0.0) || (aGamma > 21474.83)) {
      aGamma = 0.45455;
      png_set_gAMA(png, pinfo, aGamma);
    }
    png_set_gamma(png, 2.2, aGamma);
  } else {
    png_set_gamma(png, 2.2, 0.45455);
  }
}

void PngDecoder::initialize() {
  reader.remain = reader.size;
  reader.file.seekg(0, std::ios::beg);

  auto errorFn = [](png_struct *, png_const_charp msg) {
    throw std::runtime_error(msg);
  };

  auto warnFn = [](png_struct *, png_const_charp msg) {
    std::cout << msg << std::endl;
  };

  png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, errorFn, warnFn);
  if (!png) {
    throw std::runtime_error("Failed to create png read struct");
  }
  pinfo = png_create_info_struct(png);
  if (!pinfo) {
    throw std::runtime_error("Failed to create png info struct");
  }

  auto readFn = [](png_struct *p, png_byte *data, png_size_t length) {
    auto *r = (PngReader *)png_get_io_ptr(p);
    uint32_t next = std::min(r->remain, (uint32_t)length);
    if (next > 0) {
      r->file.read((char *)data, next);
      r->remain -= next;
    }
  };

  png_set_read_fn(png, &reader, readFn);
  png_read_info(png, pinfo);

  auto color_type = png_get_color_type(png, pinfo);
  if (color_type == PNG_COLOR_TYPE_PALETTE)
    png_set_palette_to_rgb(png);

  auto bit_depth = png_get_bit_depth(png, pinfo);

  if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8)
    png_set_expand_gray_1_2_4_to_8(png);

  png_set_swap(png);

  if (!_get_color_profile()) {
    if (png_get_valid(png, pinfo, PNG_INFO_gAMA) &&
        png_get_valid(png, pinfo, PNG_INFO_cHRM)) {
      png_set_gray_to_rgb(png);

      PNGDoGammaCorrection(png, pinfo);
    }
  }

  finished_reading = false;
}

bool PngDecoder::_get_color_profile() {
  if (png_get_valid(png, pinfo, PNG_INFO_iCCP)) {
    png_charp name;
    png_bytep icc_data;
    png_uint_32 icc_size;
    int comp_type;
    png_get_iCCP(png, pinfo, &name, &comp_type, &icc_data, &icc_size);

    src_profile = cmsOpenProfileFromMem(icc_data, icc_size);
    cmsColorSpaceSignature profileSpace = cmsGetColorSpace(src_profile);

    auto color_type = png_get_color_type(png, pinfo);

    bool rgb = color_type & PNG_COLOR_MASK_COLOR;

    if ((rgb && profileSpace != cmsSigRgbData) ||
        (!rgb && profileSpace != cmsSigGrayData)) {
      cmsCloseProfile(src_profile);
      src_profile = nullptr;
    } else {
      return true;
    }
  }

  if (png_get_valid(png, pinfo, PNG_INFO_gAMA) &&
      png_get_valid(png, pinfo, PNG_INFO_cHRM)) {
    cmsCIExyYTRIPLE primaries = {{0.64, 0.33, 1}, //
                                 {0.21, 0.71, 1}, //
                                 {0.15, 0.06, 1}};
    cmsCIExyY whitepoint = {0.3127, 0.3290, 1.0};

    png_get_cHRM(png, pinfo, &whitepoint.x, &whitepoint.y, //
                 &primaries.Red.x, &primaries.Red.y, &primaries.Green.x,
                 &primaries.Green.y, &primaries.Blue.x, &primaries.Blue.y);

    double gamma;
    png_get_gAMA(png, pinfo, &gamma);

    cmsToneCurve *cmsgamma[3];
    cmsgamma[0] = cmsgamma[1] = cmsgamma[2] = cmsBuildGamma(NULL, 1.0 / gamma);

    src_profile = cmsCreateRGBProfile(&whitepoint, &primaries, cmsgamma);

    cmsFreeToneCurve(cmsgamma[0]);
    return false;
  }

  if (png_get_valid(png, pinfo, PNG_INFO_sRGB)) {
    int intent;
    png_set_gray_to_rgb(png);
    png_get_sRGB(png, pinfo, &intent);
    src_profile = cmsCreate_sRGBProfile();
    cmsSetHeaderRenderingIntent(src_profile, intent);
    return true;
  }

  return false;
}

std::vector<uint8_t> PngDecoder::decode() {
  if (finished_reading)
    initialize();

  int stride = info.width * info.components * (info.bits == 16 ? 2 : 1);

  std::vector<uint8_t> pixels(info.height * stride);

  std::vector<png_bytep> row_pointers(info.height);
  for (uint32_t y = 0; y < info.height; y++) {
    row_pointers[y] = pixels.data() + (y * stride);
  }

  png_read_image(png, row_pointers.data());

  finished_reading = true;

  return pixels;
}
