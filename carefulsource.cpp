#include "carefulsource.h"

#include "decoder_jpeg.h"
#include "decoder_png.h"

#include <fstream>
#include <iostream>

template <typename T>
void unswizzle(const T *in, uint32_t stride, uint32_t planes_in, T **planes,
               ptrdiff_t *strides, uint32_t planes_out, uint32_t width,
               uint32_t height) {
  for (uint32_t p = 0; p < std::min(planes_in, planes_out); p++) {
    for (uint32_t y = 0; y < height; y++) {
      for (uint32_t x = 0; x < width; x++) {
        planes[p][y * strides[p] + x] = in[y * stride + x * planes_in + p];
      }
    }
  }
}

template <typename T>
void swizzle(const T **planes, ptrdiff_t *strides, uint32_t planes_in, T *out,
             uint32_t stride, uint32_t planes_out, uint32_t width,
             uint32_t height) {
  for (uint32_t p = 0; p < std::min(planes_in, planes_out); p++) {
    for (uint32_t y = 0; y < height; y++) {
      for (uint32_t x = 0; x < width; x++) {
        out[y * width * planes_out + x * planes_out + p] =
            planes[p][y * strides[p] + x];
      }
    }
  }
}

template <typename T>
void copy_planar(const T *in, uint32_t stride, uint32_t planes_in, T **planes,
                 ptrdiff_t *strides, uint32_t planes_out, uint32_t height) {
  for (int p = 0; p < std::min(planes_in, planes_out); p++) {
    for (int y = 0; y < height; y++) {
      memcpy(planes[p] + strides[p] * y, in + height * stride * p + stride * y,
             stride * sizeof(T));
    }
  }
}

static cmsToneCurve *Build_sRGBGamma() {
  cmsFloat64Number Parameters[5];

  Parameters[0] = 2.4;
  Parameters[1] = 1. / 1.055;
  Parameters[2] = 0.055 / 1.055;
  Parameters[3] = 1. / 12.92;
  Parameters[4] = 0.04045;

  return cmsBuildParametricToneCurve(NULL, 4, Parameters);
}

static cmsHPROFILE create_sRGB_gray() {
  cmsToneCurve *gamma22 = Build_sRGBGamma();
  cmsCIExyY D65 = {0.3127, 0.3290, 1.0};
  cmsHPROFILE profile = cmsCreateGrayProfile(&D65, gamma22);
  cmsFreeToneCurve(gamma22);
  return profile;
}

static const VSFrame *VS_CC imagesource_getframe(
    int n, int activationReason, void *instanceData, void **frameData,
    VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
  auto d = static_cast<ImageSourceData *>(instanceData);

  if (activationReason == arInitial) {
    ImageInfo info = d->decoder->info;

    VSFrame *dst = vsapi->newVideoFrame(&d->vi.format, info.width, info.height,
                                        nullptr, core);
    VSFrame *dst_alpha = nullptr;

    uint8_t *planes[4] = {};
    ptrdiff_t strides[4] = {};

    for (int p = 0; p < d->vi.format.numPlanes; p++) {
      planes[p] = vsapi->getWritePtr(dst, p);
      strides[p] = vsapi->getStride(dst, p) / d->vi.format.bytesPerSample;
    }

    if (info.has_alpha) {
      VSVideoFormat format_alpha = {};
      vsapi->queryVideoFormat(&format_alpha, VSColorFamily::cfGray,
                              d->vi.format.sampleType,
                              d->vi.format.bitsPerSample, 0, 0, core);

      dst_alpha = vsapi->newVideoFrame(&format_alpha, info.width, info.height,
                                       nullptr, core);
      planes[d->vi.format.numPlanes] = vsapi->getWritePtr(dst_alpha, 0);
      strides[d->vi.format.numPlanes] =
          vsapi->getStride(dst_alpha, 0) / format_alpha.bytesPerSample;

      vsapi->mapSetInt(vsapi->getFramePropertiesRW(dst_alpha), "_ColorRange", 0,
                       maReplace);
    }

    cmsHPROFILE src_profile = d->decoder->get_color_profile();
    if (!src_profile) {
      if (d->vi.format.colorFamily == VSColorFamily::cfGray) {
        src_profile = create_sRGB_gray();
      } else {
        src_profile = cmsCreate_sRGBProfile();
      }
    }

    cmsUInt32Number out_length;
    cmsSaveProfileToMem(src_profile, NULL, &out_length);
    std::vector<uint8_t> src_profile_bytes(out_length);
    cmsSaveProfileToMem(src_profile, src_profile_bytes.data(), &out_length);

    VSMap *props = vsapi->getFramePropertiesRW(dst);

    vsapi->mapSetData(props, "ICCProfile",
                      reinterpret_cast<const char *>(src_profile_bytes.data()),
                      out_length, dtBinary, maAppend);

    std::vector<uint8_t> pixels = d->decoder->decode();

    if (info.subsampling_w == 0 && info.subsampling_h == 0) {
      if (info.bits == 32) {
        unswizzle<uint32_t>((uint32_t *)pixels.data(),
                            info.width * info.components, info.components,
                            reinterpret_cast<uint32_t **>(planes), strides,
                            info.components, info.width, info.height);
      } else if (info.bits == 16) {
        unswizzle<uint16_t>((uint16_t *)pixels.data(),
                            info.width * info.components, info.components,
                            reinterpret_cast<uint16_t **>(planes), strides,
                            info.components, info.width, info.height);
      } else {
        unswizzle<uint8_t>(pixels.data(), info.width * info.components,
                           info.components, planes, strides, info.components,
                           info.width, info.height);
      }
    } else {
    }

    if (dst_alpha)
      vsapi->mapConsumeFrame(props, "_Alpha", dst_alpha, maReplace);

    if (info.components < 3) {
      vsapi->mapSetInt(props, "_Matrix", 2, maAppend);
      vsapi->mapSetInt(props, "_Primaries", 2, maAppend);
      vsapi->mapSetInt(props, "_Transfer", 2, maAppend);
    } else {
      vsapi->mapSetInt(props, "_Matrix", 0, maAppend);
      vsapi->mapSetInt(props, "_Primaries", 1, maAppend);
      vsapi->mapSetInt(props, "_Transfer", 1, maAppend);
    }

    vsapi->mapSetInt(props, "_ColorRange", 0, maAppend);

    return dst;
  }

  return nullptr;
}

static void VS_CC imagesource_free(void *instanceData, VSCore *core,
                                   const VSAPI *vsapi) {
  auto d = static_cast<ImageSourceData *>(instanceData);
  delete d;
}

void VS_CC imagesource_create(const VSMap *in, VSMap *out, void *userData,
                              VSCore *core, const VSAPI *vsapi) {
  ImageSourceData *d = new ImageSourceData();

  const char *file_path = vsapi->mapGetData(in, "source", 0, NULL);

  {
    std::ifstream file(file_path, std::ios_base::binary);
    file.unsetf(std::ios::skipws);
    file.seekg(0, std::ios::end);
    uint32_t filesize = file.tellg();
    file.seekg(0, std::ios::beg);
    d->data.resize(filesize);
    file.read(reinterpret_cast<char *>(d->data.data()), filesize);
  }

  if (PngDecoder::is_png(d->data.data())) {
    d->decoder = std::make_unique<PngDecoder>(&d->data);
  } else if (JpegDecoder::is_jpeg(d->data.data())) {
    d->decoder = std::make_unique<JpegDecoder>(&d->data);
  } else {
    throw std::runtime_error("file format unrecognized ");
  }

  ImageInfo info = d->decoder->info;

  std::cout << "decoder " << d->decoder->get_name() << std::endl
            << "width " << info.width << std::endl
            << "height " << info.height << std::endl
            << "components " << info.components << std::endl
            << "alpha " << info.has_alpha << std::endl
            << "color " << info.color << std::endl
            << "sample_type " << info.sample_type << std::endl
            << "bits " << info.bits << std::endl
            << "subsampling_w " << info.subsampling_w << std::endl
            << "subsampling_h " << info.subsampling_h << std::endl;

  d->vi = {
      .format = {},
      .fpsNum = 1,
      .fpsDen = 1,
      .width = (int)info.width,
      .height = (int)info.height,
      .numFrames = 1,
  };

  vsapi->queryVideoFormat(&d->vi.format, info.color, info.sample_type,
                          info.bits, info.subsampling_w, info.subsampling_h,
                          core);

  vsapi->createVideoFilter(out, "ImageSource", &d->vi, imagesource_getframe,
                           imagesource_free, fmUnordered, nullptr, 0, d, core);
}

static const VSFrame *VS_CC convertcolor_getframe(
    int n, int activationReason, void *instanceData, void **frameData,
    VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
  auto d = static_cast<ConvertColorData *>(instanceData);

  if (activationReason == arInitial) {
    vsapi->requestFrameFilter(n, d->node, frameCtx);
  } else if (activationReason == arAllFramesReady) {
    auto src = vsapi->getFrameFilter(n, d->node, frameCtx);
    const VSMap *src_props = vsapi->getFramePropertiesRO(src);

    auto src_profile_data =
        vsapi->mapGetData(src_props, "ICCProfile", 0, nullptr);
    int src_profile_size =
        vsapi->mapGetDataSize(src_props, "ICCProfile", 0, nullptr);

    cmsHPROFILE src_profile =
        cmsOpenProfileFromMem(src_profile_data, src_profile_size);

    decltype(src) fr[]{nullptr, nullptr, nullptr};
    constexpr int pl[]{0, 1, 2};

    auto dst = vsapi->newVideoFrame2(&d->vi.format, d->vi.width, d->vi.height,
                                     fr, pl, src, core);

    int n_in_planes = d->src_vi->format.numPlanes;
    int n_out_planes = d->vi.format.numPlanes;

    std::vector<uint8_t> pixels(d->vi.width * d->vi.height * n_in_planes *
                                d->src_vi->format.bytesPerSample);
    std::vector<uint8_t> pixels2(d->vi.width * d->vi.height * n_out_planes * 4);
    std::vector<uint8_t> pixels3;
    if (!d->float_output)
      pixels3.resize(d->vi.width * d->vi.height * n_out_planes *
                     d->vi.format.bytesPerSample);

    const uint8_t *src_planes[4] = {};
    ptrdiff_t src_strides[4] = {};

    uint8_t *dst_planes[4] = {};
    ptrdiff_t dst_strides[4] = {};

    for (int plane = 0; plane < d->src_vi->format.numPlanes; plane++) {
      src_planes[plane] = vsapi->getReadPtr(src, plane);
      src_strides[plane] =
          vsapi->getStride(src, plane) / d->src_vi->format.bytesPerSample;
    }

    for (int plane = 0; plane < d->vi.format.numPlanes; plane++) {
      dst_planes[plane] = vsapi->getWritePtr(dst, plane);
      dst_strides[plane] =
          vsapi->getStride(dst, plane) / d->vi.format.bytesPerSample;
    }

    // TODO: alpha

    if (d->src_vi->format.bytesPerSample == 4) {
      swizzle<uint32_t>(reinterpret_cast<const uint32_t **>(src_planes),
                        src_strides, n_in_planes, (uint32_t *)pixels.data(),
                        d->vi.width * n_in_planes, n_in_planes, d->vi.width,
                        d->vi.height);
    } else if (d->src_vi->format.bytesPerSample == 2) {
      swizzle<uint16_t>(reinterpret_cast<const uint16_t **>(src_planes),
                        src_strides, n_in_planes, (uint16_t *)pixels.data(),
                        d->vi.width * n_in_planes, n_in_planes, d->vi.width,
                        d->vi.height);
    } else {
      swizzle<uint8_t>(src_planes, src_strides, n_in_planes, pixels.data(),
                       d->vi.width * n_in_planes, n_in_planes, d->vi.width,
                       d->vi.height);
    }

    bool is_gray = d->src_vi->format.numPlanes == 1;
    bool is_float = d->src_vi->format.sampleType == VSSampleType::stFloat;
    bool is_16 = d->src_vi->format.bitsPerSample == 16;

    int intype = is_gray && is_float ? TYPE_GRAY_FLT
                 : is_gray && is_16  ? TYPE_GRAY_16
                 : is_gray           ? TYPE_GRAY_8
                 : is_float          ? TYPE_RGB_FLT
                 : is_16             ? TYPE_RGB_16
                                     : TYPE_RGB_8;

    VSMap *props = vsapi->getFramePropertiesRW(dst);

    int outtype_intermediate;
    int outtype;
    if (d->target == "xyz") {
      vsapi->mapDeleteKey(props, "ICCProfile");
      vsapi->mapSetInt(props, "_ColorRange", 0, maReplace);
      vsapi->mapSetInt(props, "_Matrix", 0, maReplace);
      vsapi->mapSetInt(props, "_Primaries", 10, maReplace);
      vsapi->mapSetInt(props, "_Transfer", 8, maReplace);

      outtype_intermediate = TYPE_XYZ_FLT;
      if (d->vi.format.sampleType == VSSampleType::stFloat) {
        outtype = TYPE_XYZ_FLT;
      } else {
        outtype = TYPE_XYZ_16;
      }
    } else {
      cmsUInt32Number out_length;
      cmsSaveProfileToMem(d->target_profile, NULL, &out_length);
      std::vector<uint8_t> target_profile_bytes(out_length);
      cmsSaveProfileToMem(d->target_profile, target_profile_bytes.data(),
                          &out_length);

      vsapi->mapSetData(
          props, "ICCProfile",
          reinterpret_cast<const char *>(target_profile_bytes.data()),
          out_length, dtBinary, maReplace);

      if (d->vi.format.colorFamily == VSColorFamily::cfGray) {
        vsapi->mapSetInt(props, "_ColorRange", 0, maReplace);
        vsapi->mapSetInt(props, "_Matrix", 2, maReplace);
        vsapi->mapSetInt(props, "_Primaries", 2, maReplace);
        vsapi->mapSetInt(props, "_Transfer", 2, maReplace);

        outtype_intermediate = TYPE_GRAY_FLT;
        if (d->vi.format.sampleType == VSSampleType::stFloat) {
          outtype = TYPE_GRAY_FLT;
        } else {
          outtype = TYPE_GRAY_16;
        }

      } else {
        vsapi->mapSetInt(props, "_ColorRange", 0, maReplace);
        vsapi->mapSetInt(props, "_Matrix", 0, maReplace);
        vsapi->mapSetInt(props, "_Primaries", 1, maReplace);
        vsapi->mapSetInt(props, "_Transfer", 1, maReplace);

        outtype_intermediate = TYPE_RGB_FLT;
        if (d->vi.format.sampleType == VSSampleType::stFloat) {
          outtype = TYPE_RGB_FLT;
        } else {
          outtype = TYPE_RGB_16;
        }
      }
    }

    cmsHTRANSFORM transform = cmsCreateTransform(
        src_profile, intype, d->target_profile,
        outtype_intermediate | PLANAR_SH(1),
        cmsGetHeaderRenderingIntent(src_profile),
        cmsFLAGS_HIGHRESPRECALC | cmsFLAGS_BLACKPOINTCOMPENSATION);

    if (!transform) {
      throw std::runtime_error("invalid transform");
    }

    cmsDoTransform(transform, pixels.data(), pixels2.data(),
                   d->vi.width * d->vi.height);

    cmsDeleteTransform(transform);

    uint8_t *out_pointer = pixels2.data();

    if (!d->float_output) {
      cmsHTRANSFORM transform2 = cmsCreateTransform(
          d->target_profile, outtype_intermediate | PLANAR_SH(1),
          d->target_profile, outtype | PLANAR_SH(1),
          cmsGetHeaderRenderingIntent(src_profile),
          cmsFLAGS_HIGHRESPRECALC | cmsFLAGS_BLACKPOINTCOMPENSATION);

      cmsDoTransform(transform2, pixels2.data(), pixels3.data(),
                     d->vi.width * d->vi.height);

      cmsDeleteTransform(transform2);

      cmsCloseProfile(src_profile);

      out_pointer = pixels3.data();
    }

    if (d->vi.format.bytesPerSample == 4) {
      copy_planar<uint32_t>(reinterpret_cast<uint32_t *>(out_pointer),
                            d->vi.width, n_out_planes,
                            reinterpret_cast<uint32_t **>(dst_planes),
                            dst_strides, n_out_planes, d->vi.height);
    } else if (d->vi.format.bytesPerSample == 2) {
      copy_planar<uint16_t>(reinterpret_cast<uint16_t *>(out_pointer),
                            d->vi.width, n_out_planes,
                            reinterpret_cast<uint16_t **>(dst_planes),
                            dst_strides, n_out_planes, d->vi.height);
    } else {
      copy_planar<uint8_t>(reinterpret_cast<uint8_t *>(out_pointer),
                           d->vi.width, n_out_planes,
                           reinterpret_cast<uint8_t **>(dst_planes),
                           dst_strides, n_out_planes, d->vi.height);
    }

    return dst;
  }

  return nullptr;
}

static void VS_CC convertcolor_free(void *instanceData, VSCore *core,
                                    const VSAPI *vsapi) {
  auto d = static_cast<ConvertColorData *>(instanceData);
  vsapi->freeNode(d->node);

  if (d->target_profile) {
    cmsCloseProfile(d->target_profile);
  }

  delete d;
}

void VS_CC convertcolor_create(const VSMap *in, VSMap *out, void *userData,
                               VSCore *core, const VSAPI *vsapi) {
  ConvertColorData *d = new ConvertColorData();

  d->node = vsapi->mapGetNode(in, "clip", 0, nullptr);
  d->src_vi = vsapi->getVideoInfo(d->node);
  d->target = std::string(vsapi->mapGetData(in, "output_profile", 0, nullptr));

  int err = 0;

  d->float_output = !!vsapi->mapGetInt(in, "float_output", 0, &err);
  if (err)
    d->float_output = d->src_vi->format.sampleType == VSSampleType::stFloat;

  err = 0;
  // TODO: input profile

  d->vi = {
      .format = {},
      .fpsNum = 1,
      .fpsDen = 1,
      .width = d->src_vi->width,
      .height = d->src_vi->height,
      .numFrames = 1,
  };

  int sample_type = d->src_vi->format.sampleType;
  if (d->float_output) {
    sample_type = VSSampleType::stFloat;
  }

  int bits;
  if (sample_type == VSSampleType::stFloat) {
    bits = 32;
  } else {
    bits = 16;
  }

  d->target_profile = nullptr;
  auto color_family = d->src_vi->format.colorFamily;
  if (d->target == "xyz") {
    d->target_profile = cmsCreateXYZProfile();
    color_family = VSColorFamily::cfRGB;
  } else if (d->target == "srgb") {
    d->target_profile = cmsCreate_sRGBProfile();
    color_family = VSColorFamily::cfRGB;
  } else if (d->target == "srgb-gray") {
    d->target_profile = create_sRGB_gray();
    color_family = VSColorFamily::cfGray;
  } else {
    d->target_profile = cmsOpenProfileFromFile(d->target.c_str(), "r");
    if (!d->target_profile) {
      throw std::runtime_error("Bad target profile");
    }
    cmsColorSpaceSignature profile_colorspace =
        cmsGetColorSpace(d->target_profile);
    if (profile_colorspace == cmsSigGrayData) {
      color_family = VSColorFamily::cfGray;
    } else if (profile_colorspace == cmsSigRgbData) {
      color_family = VSColorFamily::cfRGB;
    } else {
      throw std::runtime_error(std::string("Unhandled profile colorspace ") +
                               std::to_string(color_family));
    }
  }

  vsapi->queryVideoFormat(&d->vi.format, color_family, sample_type, bits, 0, 0,
                          core);

  VSFilterDependency deps[]{{d->node, rpStrictSpatial}};
  vsapi->createVideoFilter(out, "ConvertColor", &d->vi, convertcolor_getframe,
                           convertcolor_free, fmUnordered, deps, 1, d, core);
}

VS_EXTERNAL_API(void)
VapourSynthPluginInit2(VSPlugin *plugin, const VSPLUGINAPI *vspapi) {
  vspapi->configPlugin("moe.grass.carefulsource", "cs", "carefulsource",
                       VS_MAKE_VERSION(1, 0), VAPOURSYNTH_API_VERSION, 0,
                       plugin);
  vspapi->registerFunction("ImageSource", "source:data;", "clip:vnode;",
                           imagesource_create, nullptr, plugin);
  vspapi->registerFunction("ConvertColor",
                           "clip:vnode;"
                           "output_profile:data;"
                           "input_profile:data:opt;"
                           "float_output:int:opt;",
                           "clip:vnode;", convertcolor_create, nullptr, plugin);
}
