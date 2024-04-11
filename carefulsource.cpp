#include "carefulsource.h"

#include "decoder_png.h"
#include <iostream>
#include <memory>

template <typename T>
void unswizzle(const T *in, uint32_t stride, T **planes, ptrdiff_t *strides,
               uint32_t width, uint32_t height, uint32_t planes_in,
               uint32_t planes_out) {
  for (uint32_t p = 0; p < std::min(planes_in, planes_out); p++) {
    for (uint32_t y = 0; y < height; y++) {
      for (uint32_t x = 0; x < width; x++) {
        planes[p][y * strides[p] + x] = in[y * stride + x * planes_in + p];
      }
    }
  }
}

template <typename T>
void swizzle(const T **planes, ptrdiff_t *strides, T *out, uint32_t stride,
             uint32_t width, uint32_t height, uint32_t planes_in,
             uint32_t planes_out) {
  for (uint32_t p = 0; p < std::min(planes_in, planes_out); p++) {
    for (uint32_t y = 0; y < height; y++) {
      for (uint32_t x = 0; x < width; x++) {
        out[y * width * planes_out + x * planes_out + p] =
            planes[p][y * strides[p] + x];
      }
    }
  }
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
      src_profile = cmsCreate_sRGBProfile();
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

    if (info.bits == 16) {
      unswizzle<uint16_t>(
          (uint16_t *)pixels.data(), info.width * info.components,
          reinterpret_cast<uint16_t **>(planes), strides, info.width,
          info.height, info.components, info.components);
    } else {
      unswizzle<uint8_t>(pixels.data(), info.width * info.components, planes,
                         strides, info.width, info.height, info.components,
                         info.components);
    }

    if (dst_alpha)
      vsapi->mapConsumeFrame(props, "_Alpha", dst_alpha, maReplace);

    if (info.components < 3) {
      vsapi->mapSetInt(props, "_Matrix", 2, maAppend);
    } else {
      vsapi->mapSetInt(props, "_Matrix", 0, maAppend);
    }

    vsapi->mapSetInt(props, "_ColorRange", 0, maAppend);
    vsapi->mapSetInt(props, "_Primaries", 1, maAppend);
    vsapi->mapSetInt(props, "_Transfer", 13, maAppend);

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

  d->decoder = std::make_unique<PngDecoder>(file_path);
  ImageInfo info = d->decoder->info;

  std::cout << "width " << info.width << std::endl
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

    std::cout << "frame has profile size " << src_profile_size << std::endl;

    cmsHPROFILE src_profile =
        cmsOpenProfileFromMem(src_profile_data, src_profile_size);

    decltype(src) fr[]{nullptr, nullptr, nullptr};
    constexpr int pl[]{0, 1, 2};

    auto dst = vsapi->newVideoFrame2(&d->vi.format, d->vi.width, d->vi.height,
                                     fr, pl, src, core);

    std::vector<uint8_t> pixels(d->vi.width * d->vi.height * 4 *
                                d->src_vi->format.bytesPerSample);
    std::vector<uint8_t> pixels2(d->vi.width * d->vi.height * 4 *
                                 d->vi.format.bytesPerSample);

    const uint8_t *src_planes[3] = {};
    ptrdiff_t src_strides[3] = {};

    uint8_t *dst_planes[3] = {};
    ptrdiff_t dst_strides[3] = {};

    for (int plane = 0; plane < d->vi.format.numPlanes; plane++) {
      src_planes[plane] = vsapi->getReadPtr(src, plane);
      src_strides[plane] =
          vsapi->getStride(src, plane) / d->src_vi->format.bytesPerSample;
      dst_planes[plane] = vsapi->getWritePtr(dst, plane);
      dst_strides[plane] =
          vsapi->getStride(dst, plane) / d->vi.format.bytesPerSample;
    }

    if (d->src_vi->format.bytesPerSample == 2) {
      swizzle<uint16_t>(reinterpret_cast<const uint16_t **>(src_planes),
                        src_strides, (uint16_t *)pixels.data(), d->vi.width * 3,
                        d->vi.width, d->vi.height, 3, 3);
    } else {
      swizzle<uint8_t>(src_planes, src_strides, pixels.data(), d->vi.width * 3,
                       d->vi.width, d->vi.height, 3, 3);
    }

    int intype;
    if (d->src_vi->format.sampleType == VSSampleType::stFloat) {
      intype = TYPE_RGB_FLT;
    } else {
      if (d->src_vi->format.bitsPerSample == 16) {
        intype = TYPE_RGB_16;
      } else {
        intype = TYPE_RGB_8;
      }
    }

    cmsHPROFILE target_profile;

    std::cout << "target " << d->target << std::endl;

    VSMap *props = vsapi->getFramePropertiesRW(dst);

    int outtype;
    if (d->target == "xyz") {
      vsapi->mapDeleteKey(props, "ICCProfile");
      vsapi->mapSetInt(props, "_Primaries", 10, maReplace);
      vsapi->mapSetInt(props, "_Transfer", 8, maReplace);

      target_profile = cmsCreateXYZProfile();
      if (d->vi.format.sampleType == VSSampleType::stFloat) {
        outtype = TYPE_XYZ_FLT;
      } else {
        outtype = TYPE_XYZ_16;
      }
    } else {
      if (d->target == "srgb") {
        target_profile = cmsCreate_sRGBProfile();
      } else {
        target_profile = cmsOpenProfileFromFile(d->target.c_str(), "r");
      }

      cmsUInt32Number out_length;
      cmsSaveProfileToMem(target_profile, NULL, &out_length);
      std::vector<uint8_t> target_profile_bytes(out_length);
      cmsSaveProfileToMem(target_profile, target_profile_bytes.data(),
                          &out_length);

      vsapi->mapSetData(
          props, "ICCProfile",
          reinterpret_cast<const char *>(target_profile_bytes.data()),
          out_length, dtBinary, maReplace);

      vsapi->mapSetInt(props, "_ColorRange", 0, maReplace);
      vsapi->mapSetInt(props, "_Matrix", 0, maReplace);
      vsapi->mapSetInt(props, "_Primaries", 1, maReplace);
      vsapi->mapSetInt(props, "_Transfer", 13, maReplace);

      if (d->vi.format.sampleType == VSSampleType::stFloat) {
        outtype = TYPE_RGB_FLT;
      } else {
        if (d->vi.format.bitsPerSample == 16) {
          outtype = TYPE_RGB_16;
        } else {
          outtype = TYPE_RGB_8;
        }
      }
    }

    // TODO: gray

    cmsHTRANSFORM transform =
        cmsCreateTransform(src_profile, intype, target_profile, outtype,
                           cmsGetHeaderRenderingIntent(src_profile), 0);

    if (!transform) {
      std::cout << "invalid transform" << std::endl;
    }

    cmsDoTransform(transform, pixels.data(), pixels2.data(),
                   d->vi.width * d->vi.height);

    cmsDeleteTransform(transform);
    cmsCloseProfile(src_profile);
    cmsCloseProfile(target_profile);

    if (d->vi.format.bytesPerSample == 2) {
      unswizzle<uint16_t>((uint16_t *)pixels2.data(), d->vi.width * 3,
                          reinterpret_cast<uint16_t **>(dst_planes),
                          dst_strides, d->vi.width, d->vi.height, 3, 3);
    } else {
      unswizzle<uint8_t>(pixels2.data(), d->vi.width * 3, dst_planes,
                         dst_strides, d->vi.width, d->vi.height, 3, 3);
    }

    return dst;
  }

  return nullptr;
}

static void VS_CC convertcolor_free(void *instanceData, VSCore *core,
                                    const VSAPI *vsapi) {
  auto d = static_cast<ConvertColorData *>(instanceData);
  vsapi->freeNode(d->node);
  delete d;
}

void VS_CC convertcolor_create(const VSMap *in, VSMap *out, void *userData,
                               VSCore *core, const VSAPI *vsapi) {
  ConvertColorData *d = new ConvertColorData();

  d->node = vsapi->mapGetNode(in, "clip", 0, nullptr);
  const char *target = vsapi->mapGetData(in, "target", 0, NULL);
  d->target = target;

  d->src_vi = vsapi->getVideoInfo(d->node);

  d->vi = {
      .format = {},
      .fpsNum = 1,
      .fpsDen = 1,
      .width = d->src_vi->width,
      .height = d->src_vi->height,
      .numFrames = 1,
  };

  int bits;
  if (d->target == "xyz") {
    if (d->src_vi->format.sampleType == VSSampleType::stFloat) {
      bits = 32;
    } else {
      bits = 16;
    }
  } else {
    if (d->src_vi->format.sampleType == VSSampleType::stFloat) {
      bits = 32;
    } else {
      bits = d->src_vi->format.bitsPerSample;
    }
  }

  vsapi->queryVideoFormat(&d->vi.format, d->src_vi->format.colorFamily,
                          d->src_vi->format.sampleType, bits, 0, 0, core);

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
  vspapi->registerFunction("ConvertColor", "clip:vnode;target:data;",
                           "clip:vnode;", convertcolor_create, nullptr, plugin);
}
