#include "carefulsource.h"

#include <fstream>

template <typename T>
void unswizzle(const T *in, uint32_t stride, uint32_t planes_in, T **planes,
               ptrdiff_t *strides, uint32_t planes_out, uint32_t width,
               uint32_t height) {
  for (uint32_t p = 0; p < std::min(planes_in, planes_out); p++) {
    for (uint32_t y = 0; y < height; y++) {
      for (uint32_t x = 0; x < width; x++) {
        planes[p][y * strides[p] + x] =
            in[(size_t)y * stride + (size_t)x * planes_in + p];
      }
    }
  }
}

template <typename T>
void swizzle(const T **planes, ptrdiff_t *strides, uint32_t planes_in, T *out,
             uint32_t planes_out, uint32_t width, uint32_t height) {
  for (uint32_t p = 0; p < std::min(planes_in, planes_out); p++) {
    for (uint32_t y = 0; y < height; y++) {
      for (uint32_t x = 0; x < width; x++) {
        out[(size_t)y * width * planes_out + (size_t)x * planes_out + p] =
            planes[p][y * strides[p] + x];
      }
    }
  }
}

template <typename T>
void copy_planar(const T *in, uint32_t stride, uint32_t planes_in, T **planes,
                 ptrdiff_t *strides, uint32_t planes_out, uint32_t height) {
  for (uint32_t p = 0; p < std::min(planes_in, planes_out); p++) {
    for (uint32_t y = 0; y < height; y++) {
      memcpy(planes[p] + strides[p] * y,
             in + (size_t)height * stride * p + (size_t)stride * y,
             stride * sizeof(T));
    }
  }
}

template <typename T>
void copy_plane(const T *in, uint32_t in_stride, T *out, ptrdiff_t out_stride,
                uint32_t width, uint32_t height) {
  for (uint32_t y = 0; y < height; y++) {
    memcpy(out + out_stride * y, in + (size_t)in_stride * y, width * sizeof(T));
  }
}

template <typename T>
void copy_planar_yuv(const uint8_t *in, const ImageInfo &info, uint8_t **planes,
                     ptrdiff_t *strides, uint32_t width, uint32_t height) {
  uint32_t sw = padded_width(info);
  uint32_t sh = padded_height(info);
  uint32_t spw = sw >> info.subsampling_w;
  uint32_t sph = sh >> info.subsampling_h;

  const T *src = reinterpret_cast<const T *>(in);
  copy_plane<T>(src, sw, reinterpret_cast<T *>(planes[0]), strides[0], width,
                height);
  copy_plane<T>(src + (size_t)sw * sh, spw, reinterpret_cast<T *>(planes[1]),
                strides[1], width >> info.subsampling_w,
                height >> info.subsampling_h);
  copy_plane<T>(src + (size_t)sw * sh + (size_t)spw * sph, spw,
                reinterpret_cast<T *>(planes[2]), strides[2],
                width >> info.subsampling_w, height >> info.subsampling_h);

  if (info.has_alpha) {
    copy_plane<T>(src + (size_t)sw * sh + 2 * (size_t)spw * sph, sw,
                  reinterpret_cast<T *>(planes[3]), strides[3], width, height);
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
  if (!gamma22) {
    return nullptr;
  }
  cmsCIExyY D65 = {0.3127, 0.3290, 1.0};
  cmsHPROFILE profile = cmsCreateGrayProfile(&D65, gamma22);
  cmsFreeToneCurve(gamma22);
  return profile;
}

// The message of the exception being handled; for use inside catch (...).
static std::string current_error() {
  try {
    throw;
  } catch (const std::exception &e) {
    return e.what();
  } catch (...) {
    return "unknown error";
  }
}

static size_t decoded_bytes(const ImageInfo &info) {
  const size_t bytes = info.bits <= 8 ? 1 : info.bits <= 16 ? 2 : 4;

  if (info.color == ColorFamily::YUV &&
      (info.subsampling_w != 0 || info.subsampling_h != 0)) {
    const size_t sw = padded_width(info);
    const size_t sh = padded_height(info);
    const size_t spw = sw >> info.subsampling_w;
    const size_t sph = sh >> info.subsampling_h;
    return (sw * sh * (info.has_alpha ? 2 : 1) + 2 * spw * sph) * bytes;
  }

  return (size_t)info.width * info.height * info.components * bytes;
}

static constexpr size_t RECENT_FRAMES = 4;

static void remember(ImageSourceData *d, uint32_t index,
                     std::shared_ptr<const std::vector<uint8_t>> pixels,
                     uint32_t duration_ms) {
  if (d->recent.size() >= RECENT_FRAMES) {
    d->recent.erase(d->recent.begin());
  }
  d->recent.push_back({index, std::move(pixels), duration_ms});
}

static void restart_decoder(ImageSourceData *d) {
  const uint8_t *src = d->decoder->source_data();
  const size_t size = d->decoder->source_size();
  auto fresh = create_decoder(src, size);
  if (!fresh) {
    throw std::runtime_error("image is no longer decodable");
  }
  fresh->set_data(src, size);
  if (!fresh->read_header()) {
    throw std::runtime_error("truncated image");
  }
  d->decoder = std::move(fresh);
  d->started = false;
  d->produced = 0;
}

// Frames composite, so going backwards restarts; forwards costs one step.
static DecodedFrame decode_frame(ImageSourceData *d, uint32_t want) {
  for (const auto &c : d->recent) {
    if (c.index == want) {
      return {c.pixels, c.duration_ms};
    }
  }

  if (d->started && want <= d->produced) {
    restart_decoder(d);
  }

  // All the data is here, so each call either advances or ends.
  uint32_t idle = 0;
  for (;;) {
    DecodeProgress p = d->decoder->decode(d->options);
    if (p.changed && p.finished) {
      d->produced = p.frame;
      d->started = true;
      // Frames passed on the way are kept too: they are the likeliest next.
      auto pixels =
          std::make_shared<const std::vector<uint8_t>>(d->decoder->buffer());
      remember(d, p.frame, pixels, p.duration_ms);
      if (p.frame == want) {
        return {pixels, p.duration_ms};
      }
    }
    if (p.complete) {
      break;
    }
    if (!p.changed && ++idle > 16) {
      throw std::runtime_error("decoder stopped making progress");
    }
    if (p.changed) {
      idle = 0;
    }
  }
  throw std::runtime_error("image has no frame " + std::to_string(want));
}

static const VSFrame *VS_CC imagesource_getframe(
    int n, int activationReason, void *instanceData, void **frameData,
    VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
  (void)frameData;
  auto d = static_cast<ImageSourceData *>(instanceData);

  if (activationReason != arInitial) {
    return nullptr;
  }

  const ImageInfo &info = d->info;
  VSFrame *dst = nullptr;
  VSFrame *dst_alpha = nullptr;

  try {
    DecodedFrame decoded;
    {
      std::lock_guard<std::mutex> guard(d->lock);
      decoded = decode_frame(d, (uint32_t)n);
    }
    const std::vector<uint8_t> &pixels = *decoded.pixels;

    if (pixels.size() < decoded_bytes(info)) {
      throw std::runtime_error("decoder returned a short buffer");
    }

    dst = vsapi->newVideoFrame(&d->vi.format, d->vi.width, d->vi.height,
                               nullptr, core);

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

      dst_alpha = vsapi->newVideoFrame(&format_alpha, d->vi.width, d->vi.height,
                                       nullptr, core);
      planes[d->vi.format.numPlanes] = vsapi->getWritePtr(dst_alpha, 0);
      strides[d->vi.format.numPlanes] =
          vsapi->getStride(dst_alpha, 0) / format_alpha.bytesPerSample;

      vsapi->mapSetInt(vsapi->getFramePropertiesRW(dst_alpha), "_ColorRange", 0,
                       maReplace);
    }

    VSMap *props = vsapi->getFramePropertiesRW(dst);

    if (!d->profile_bytes.empty()) {
      vsapi->mapSetData(props, "ICCProfile",
                        reinterpret_cast<const char *>(d->profile_bytes.data()),
                        (int)d->profile_bytes.size(), dtBinary, maAppend);
    }

    if (info.color == ColorFamily::YUV &&
        (info.subsampling_w != 0 || info.subsampling_h != 0)) {
      const uint32_t w = (uint32_t)d->vi.width;
      const uint32_t h = (uint32_t)d->vi.height;
      if (info.bits == 32) {
        copy_planar_yuv<uint32_t>(pixels.data(), info, planes, strides, w, h);
      } else if (info.bits == 16) {
        copy_planar_yuv<uint16_t>(pixels.data(), info, planes, strides, w, h);
      } else {
        copy_planar_yuv<uint8_t>(pixels.data(), info, planes, strides, w, h);
      }
    } else if (info.components >
               (uint32_t)d->vi.format.numPlanes + (info.has_alpha ? 1u : 0u)) {
      throw std::runtime_error("more components than planes to hold them");
    } else if (info.bits == 32) {
      unswizzle<uint32_t>((const uint32_t *)pixels.data(),
                          info.width * info.components, info.components,
                          reinterpret_cast<uint32_t **>(planes), strides,
                          info.components, info.width, info.height);
    } else if (info.bits == 16) {
      unswizzle<uint16_t>((const uint16_t *)pixels.data(),
                          info.width * info.components, info.components,
                          reinterpret_cast<uint16_t **>(planes), strides,
                          info.components, info.width, info.height);
    } else {
      unswizzle<uint8_t>(pixels.data(), info.width * info.components,
                         info.components, planes, strides, info.components,
                         info.width, info.height);
    }

    if (dst_alpha) {
      vsapi->mapConsumeFrame(props, "_Alpha", dst_alpha, maReplace);
      dst_alpha = nullptr;
    }

    if (d->vi.format.colorFamily == VSColorFamily::cfGray) {
      vsapi->mapSetInt(props, "_Matrix", 2, maAppend);
      vsapi->mapSetInt(props, "_Primaries", 2, maAppend);
      vsapi->mapSetInt(props, "_Transfer", 2, maAppend);
    } else if (d->vi.format.colorFamily == VSColorFamily::cfYUV) {
      vsapi->mapSetInt(props, "_Matrix", info.yuv_matrix, maAppend);
      vsapi->mapSetInt(props, "_Primaries", 1, maAppend);
      vsapi->mapSetInt(props, "_Transfer", 1, maAppend);
    } else {
      vsapi->mapSetInt(props, "_Matrix", 0, maAppend);
      vsapi->mapSetInt(props, "_Primaries", 1, maAppend);
      vsapi->mapSetInt(props, "_Transfer", 1, maAppend);
    }
    // HDR samples are PQ, HLG or linear coded, not sRGB.
    const int transfer = d->hdr_kind == imagedecoder::HdrKind::PQ       ? 16
                         : d->hdr_kind == imagedecoder::HdrKind::HLG    ? 18
                         : d->hdr_kind == imagedecoder::HdrKind::Linear ? 8
                                                                        : 0;
    if (transfer) {
      vsapi->mapSetInt(props, "_Transfer", transfer, maReplace);
      vsapi->mapSetInt(props, "_Primaries", d->hdr_primaries, maReplace);
    }

    vsapi->mapSetInt(props, "_ColorRange", info.full_range ? 0 : 1, maAppend);

    if (info.color == ColorFamily::YUV && info.subsampling_w &&
        d->format_name == "JPEG") {
      vsapi->mapSetInt(props, "_ChromaLocation", 1, maReplace); // Centre.
    }

    if (d->vi.numFrames > 1) {
      // Browsers show a zero delay as 100ms, and so does this.
      const uint32_t ms = decoded.duration_ms ? decoded.duration_ms : 100;
      vsapi->mapSetInt(props, "_DurationNum", ms, maReplace);
      vsapi->mapSetInt(props, "_DurationDen", 1000, maReplace);
    }

    vsapi->mapSetData(props, "ImageFormat", d->format_name.c_str(),
                      (int)d->format_name.size(), dtUtf8, maAppend);

    if (d->vi.width != (int)info.width || d->vi.height != (int)info.height) {
      vsapi->mapSetInt(props, "ActualWidth", info.width, maAppend);
      vsapi->mapSetInt(props, "ActualHeight", info.height, maAppend);
    }

    return dst;
  } catch (const std::exception &e) {
    if (dst_alpha) {
      vsapi->freeFrame(dst_alpha);
    }
    if (dst) {
      vsapi->freeFrame(dst);
    }
    vsapi->setFilterError((std::string("ImageSource: ") + e.what()).c_str(),
                          frameCtx);
    return nullptr;
  } catch (...) {
    if (dst_alpha) {
      vsapi->freeFrame(dst_alpha);
    }
    if (dst) {
      vsapi->freeFrame(dst);
    }
    vsapi->setFilterError("ImageSource: decode failed", frameCtx);
    return nullptr;
  }
}

static void VS_CC imagesource_free(void *instanceData, VSCore *core,
                                   const VSAPI *vsapi) {
  (void)core;
  (void)vsapi;
  auto d = static_cast<ImageSourceData *>(instanceData);
  delete d;
}

void VS_CC imagesource_create(const VSMap *in, VSMap *out, void *userData,
                              VSCore *core, const VSAPI *vsapi) {
  (void)userData;
  auto d = std::make_unique<ImageSourceData>();

  try {
    const char *file_path = vsapi->mapGetData(in, "source", 0, nullptr);
    if (!file_path) {
      throw std::runtime_error("no source given");
    }

    int err = 0;
    int64_t to_rgb = vsapi->mapGetInt(in, "yuv_to_rgb", 0, &err);
    if (err) {
      to_rgb = 0;
    }

    std::vector<uint8_t> data;
    {
      std::ifstream file(file_path, std::ios_base::binary);
      if (!file.good()) {
        throw std::runtime_error(std::string("cannot open ") + file_path);
      }

      file.seekg(0, std::ios::end);
      const std::streamoff filesize = file.tellg();
      if (filesize < 0) {
        throw std::runtime_error(std::string("cannot size ") + file_path);
      }
      file.seekg(0, std::ios::beg);

      data.resize((size_t)filesize);
      if (filesize &&
          !file.read(reinterpret_cast<char *>(data.data()), filesize)) {
        throw std::runtime_error(std::string("cannot read ") + file_path);
      }
    }

    if (data.size() < SNIFF_BYTES) {
      throw std::runtime_error("file too short to identify");
    }

    d->decoder = create_decoder(data.data(), data.size());
    if (!d->decoder) {
      std::string known;
      for (const std::string &f : imagedecoder::supported_formats()) {
        known += known.empty() ? "" : ", ";
        known += f;
      }
      throw std::runtime_error("file format unrecognized; this build reads " +
                               known);
    }

    d->decoder->set_data(data.data(), data.size());
    data.clear();
    data.shrink_to_fit();

    if (!d->decoder->read_header()) {
      throw std::runtime_error("truncated image");
    }
    d->format_name = d->decoder->get_name();

    d->options.output_mode =
        to_rgb ? OutputMode::YUVtoRGB : OutputMode::Original;

    // The first frame gives the layout and is cached: it is requested first.
    DecodeProgress first;
    for (uint32_t idle = 0;;) {
      first = d->decoder->decode(d->options);
      if (first.finished || first.complete) {
        break;
      }
      idle = first.changed ? 0 : idle + 1;
      if (idle > 16) {
        throw std::runtime_error("decoder stopped making progress");
      }
    }
    if (!first.finished) {
      throw std::runtime_error("image produced no pixels");
    }
    d->info = first.info;
    const ImageInfo &info = d->info;
    d->hdr_kind = d->decoder->hdr_kind();
    d->hdr_primaries = d->decoder->hdr_primaries();

    uint32_t frames = d->decoder->frame_count();
    d->started = true;
    d->produced = first.frame;
    if (first.complete) {
      // A still: the decoder is done with its buffer, so it is taken.
      frames = 1;
      remember(d.get(), first.frame,
               std::make_shared<const std::vector<uint8_t>>(
                   d->decoder->take_buffer()),
               first.duration_ms);
    } else {
      remember(
          d.get(), first.frame,
          std::make_shared<const std::vector<uint8_t>>(d->decoder->buffer()),
          first.duration_ms);
      if (frames <= 1) {
        // JXL and HEIF sequences count frames only as they decode them.
        DecodeProgress p = first;
        uint32_t last = first.frame;
        for (uint32_t idle = 0; !p.complete;) {
          p = d->decoder->decode(d->options);
          if (p.changed && p.finished) {
            last = p.frame;
          }
          idle = p.changed ? 0 : idle + 1;
          if (idle > 16) {
            throw std::runtime_error("decoder stopped making progress");
          }
        }
        frames = last + 1;
        restart_decoder(d.get());
      }
    }

    const uint32_t out_width = padded_width(info);
    const uint32_t out_height = padded_height(info);
    if (!out_width || !out_height) {
      throw std::runtime_error("image has no pixels");
    }

    d->vi = {
        .format = {},
        .fpsNum = 1,
        .fpsDen = 1,
        .width = (int)out_width,
        .height = (int)out_height,
        .numFrames = (int)(frames ? frames : 1),
    };

    if (!vsapi->queryVideoFormat(&d->vi.format, vs_color(info.color),
                                 vs_sample_type(info.sample_type), info.bits,
                                 info.subsampling_w, info.subsampling_h,
                                 core)) {
      throw std::runtime_error("image format has no VapourSynth equivalent");
    }

    {
      cmsHPROFILE profile = d->decoder->get_color_profile();
      cmsHPROFILE fallback = nullptr;
      const bool hdr = d->hdr_kind == imagedecoder::HdrKind::PQ ||
                       d->hdr_kind == imagedecoder::HdrKind::HLG ||
                       d->hdr_kind == imagedecoder::HdrKind::Linear;
      // An sRGB stand-in would misdescribe HDR samples; they carry CICP tags.
      if (!profile && !hdr) {
        fallback = d->vi.format.colorFamily == VSColorFamily::cfGray
                       ? create_sRGB_gray()
                       : cmsCreate_sRGBProfile();
        profile = fallback;
      }

      cmsUInt32Number length = 0;
      if (profile && cmsSaveProfileToMem(profile, nullptr, &length) && length) {
        d->profile_bytes.resize(length);
        if (cmsSaveProfileToMem(profile, d->profile_bytes.data(), &length)) {
          d->profile_bytes.resize(length);
        } else {
          d->profile_bytes.clear();
        }
      }

      if (fallback) {
        cmsCloseProfile(fallback);
      }
    }

    vsapi->createVideoFilter(out, "ImageSource", &d->vi, imagesource_getframe,
                             imagesource_free, fmParallel, nullptr, 0, d.get(),
                             core);
    d.release();
  } catch (const std::exception &e) {
    vsapi->mapSetError(out, (std::string("ImageSource: ") + e.what()).c_str());
  } catch (...) {
    vsapi->mapSetError(out, "ImageSource: failed to open the image");
  }
}

template <typename TIn, typename TOut>
static void rescale_alpha(const uint8_t *in, ptrdiff_t in_stride, uint8_t *out,
                          ptrdiff_t out_stride, int width, int height,
                          double in_max, double out_max, bool round) {
  for (int y = 0; y < height; y++) {
    const TIn *src = reinterpret_cast<const TIn *>(in + in_stride * y);
    TOut *dst = reinterpret_cast<TOut *>(out + out_stride * y);
    for (int x = 0; x < width; x++) {
      double v = (double)src[x] / in_max;
      if (v < 0.0) {
        v = 0.0;
      } else if (v > 1.0) {
        v = 1.0;
      }
      dst[x] = (TOut)(v * out_max + (round ? 0.5 : 0.0));
    }
  }
}

static VSFrame *convert_alpha(const VSFrame *alpha, const VSVideoFormat &out,
                              int width, int height, VSCore *core,
                              const VSAPI *vsapi) {
  const VSVideoFormat *in = vsapi->getVideoFrameFormat(alpha);

  if (vsapi->getFrameWidth(alpha, 0) != width ||
      vsapi->getFrameHeight(alpha, 0) != height) {
    throw std::runtime_error("alpha does not match the image it came with");
  }

  const bool in_float = in->sampleType == VSSampleType::stFloat;
  const bool out_float = out.sampleType == VSSampleType::stFloat;

  if (in_float ? in->bitsPerSample != 32
               : (in->bitsPerSample != 8 && in->bitsPerSample != 16)) {
    throw std::runtime_error("alpha must be 8 or 16 bit integer, or float");
  }
  if (!out_float && out.bitsPerSample != 16) {
    throw std::runtime_error("this function should not produce 8 bit alpha");
  }

  VSVideoFormat gray = {};
  if (!vsapi->queryVideoFormat(&gray, VSColorFamily::cfGray, out.sampleType,
                               out.bitsPerSample, 0, 0, core)) {
    throw std::runtime_error("alpha format has no VapourSynth equivalent");
  }

  const double in_max =
      in_float ? 1.0 : (double)((1u << in->bitsPerSample) - 1u);
  const double out_max =
      out_float ? 1.0 : (double)((1u << out.bitsPerSample) - 1u);

  VSFrame *dst = vsapi->newVideoFrame(&gray, width, height, nullptr, core);
  const uint8_t *src_ptr = vsapi->getReadPtr(alpha, 0);
  const ptrdiff_t src_stride = vsapi->getStride(alpha, 0);
  uint8_t *dst_ptr = vsapi->getWritePtr(dst, 0);
  const ptrdiff_t dst_stride = vsapi->getStride(dst, 0);

  if (out_float) {
    if (in_float) {
      rescale_alpha<float, float>(src_ptr, src_stride, dst_ptr, dst_stride,
                                  width, height, in_max, out_max, false);
    } else if (in->bitsPerSample == 16) {
      rescale_alpha<uint16_t, float>(src_ptr, src_stride, dst_ptr, dst_stride,
                                     width, height, in_max, out_max, false);
    } else {
      rescale_alpha<uint8_t, float>(src_ptr, src_stride, dst_ptr, dst_stride,
                                    width, height, in_max, out_max, false);
    }
  } else {
    if (in_float) {
      rescale_alpha<float, uint16_t>(src_ptr, src_stride, dst_ptr, dst_stride,
                                     width, height, in_max, out_max, true);
    } else if (in->bitsPerSample == 16) {
      rescale_alpha<uint16_t, uint16_t>(src_ptr, src_stride, dst_ptr,
                                        dst_stride, width, height, in_max,
                                        out_max, true);
    } else {
      rescale_alpha<uint8_t, uint16_t>(src_ptr, src_stride, dst_ptr, dst_stride,
                                       width, height, in_max, out_max, true);
    }
  }

  vsapi->mapSetInt(vsapi->getFramePropertiesRW(dst), "_ColorRange", 0,
                   maReplace);
  return dst;
}

static const VSFrame *VS_CC convertcolor_getframe(
    int n, int activationReason, void *instanceData, void **frameData,
    VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
  (void)frameData;
  auto d = static_cast<ConvertColorData *>(instanceData);

  if (activationReason == arInitial) {
    vsapi->requestFrameFilter(n, d->node, frameCtx);
  } else if (activationReason == arAllFramesReady) {
    const VSFrame *src = vsapi->getFrameFilter(n, d->node, frameCtx);
    const VSMap *src_props = vsapi->getFramePropertiesRO(src);

    cmsHPROFILE src_profile = nullptr;
    cmsHTRANSFORM transform = nullptr;
    cmsHTRANSFORM transform2 = nullptr;
    VSFrame *dst = nullptr;
    const VSFrame *src_alpha = nullptr;
    VSFrame *dst_alpha = nullptr;

    try {
      std::string key;
      int icc_err = 0;
      const char *icc = vsapi->mapGetData(src_props, "ICCProfile", 0, &icc_err);
      if (d->input_profile) {
        key = "\1input";
      } else if (!icc_err) {
        key.assign(icc, (size_t)vsapi->mapGetDataSize(src_props, "ICCProfile",
                                                      0, nullptr));
      } else {
        key = "\1default";
      }
      ConvertColorData::Transforms tf;
      {
        std::lock_guard<std::mutex> guard(d->lock);
        auto it = d->transforms.find(key);
        if (it != d->transforms.end()) {
          tf = it->second;
        }
      }
      const bool cached = tf.to_target != nullptr;

      if (cached) {
      } else if (d->input_profile) {
        src_profile = d->input_profile;
      } else {
        int err = 0;
        auto src_profile_data =
            vsapi->mapGetData(src_props, "ICCProfile", 0, &err);
        if (!err) {
          int src_profile_size =
              vsapi->mapGetDataSize(src_props, "ICCProfile", 0, nullptr);

          src_profile =
              cmsOpenProfileFromMem(src_profile_data, src_profile_size);
          if (!src_profile) {
            throw std::runtime_error("Embedded ICC profile is broken");
          }
        } else {
          if (d->src_vi->format.colorFamily == VSColorFamily::cfRGB) {
            src_profile = cmsCreate_sRGBProfile();
            vsapi->logMessage(mtInformation,
                              "no embedded profile, assuming sRGB", core);
          } else if (d->src_vi->format.colorFamily == VSColorFamily::cfGray) {
            src_profile = create_sRGB_gray();
            vsapi->logMessage(mtInformation,
                              "no embedded profile, assuming sRGB-Gray", core);
          } else {
            throw std::runtime_error(
                "no embedded profile and no default for this colour family");
          }
          if (!src_profile) {
            throw std::runtime_error("cannot build a default profile");
          }
        }
      }

      const VSFrame *fr[]{nullptr, nullptr, nullptr};
      constexpr int pl[]{0, 1, 2};

      dst = vsapi->newVideoFrame2(&d->vi.format, d->vi.width, d->vi.height, fr,
                                  pl, src, core);

      int n_in_planes = d->src_vi->format.numPlanes;
      int n_out_planes = d->vi.format.numPlanes;

      const size_t npixels = (size_t)d->vi.width * (size_t)d->vi.height;
      std::vector<uint8_t> pixels(npixels * n_in_planes *
                                  d->src_vi->format.bytesPerSample);
      std::vector<uint8_t> pixels2(npixels * n_out_planes * 4);
      std::vector<uint8_t> pixels3;
      if (!d->float_output) {
        pixels3.resize(npixels * n_out_planes * d->vi.format.bytesPerSample);
      }

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

      if (d->src_vi->format.bytesPerSample == 4) {
        swizzle<uint32_t>(reinterpret_cast<const uint32_t **>(src_planes),
                          src_strides, n_in_planes, (uint32_t *)pixels.data(),
                          n_in_planes, d->vi.width, d->vi.height);
      } else if (d->src_vi->format.bytesPerSample == 2) {
        swizzle<uint16_t>(reinterpret_cast<const uint16_t **>(src_planes),
                          src_strides, n_in_planes, (uint16_t *)pixels.data(),
                          n_in_planes, d->vi.width, d->vi.height);
      } else {
        swizzle<uint8_t>(src_planes, src_strides, n_in_planes, pixels.data(),
                         n_in_planes, d->vi.width, d->vi.height);
      }

      bool is_gray = d->src_vi->format.numPlanes == 1;
      bool is_float = d->src_vi->format.sampleType == VSSampleType::stFloat;
      bool is_16 = d->src_vi->format.bitsPerSample == 16;

      int intype = d->input_xyz && is_float ? TYPE_XYZ_FLT
                   : d->input_xyz           ? TYPE_XYZ_16
                   : is_gray && is_float    ? TYPE_GRAY_FLT
                   : is_gray && is_16       ? TYPE_GRAY_16
                   : is_gray                ? TYPE_GRAY_8
                   : is_float               ? TYPE_RGB_FLT
                   : is_16                  ? TYPE_RGB_16
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
        vsapi->mapSetData(
            props, "ICCProfile",
            reinterpret_cast<const char *>(d->target_profile_bytes.data()),
            (int)d->target_profile_bytes.size(), dtBinary, maReplace);

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

      if (!cached) {
        const cmsUInt32Number intent = cmsGetHeaderRenderingIntent(src_profile);
        // NOCACHE: a transform shared across threads must hold no state.
        const cmsUInt32Number flags = cmsFLAGS_HIGHRESPRECALC |
                                      cmsFLAGS_BLACKPOINTCOMPENSATION |
                                      cmsFLAGS_NOCACHE;
        std::lock_guard<std::mutex> guard(d->lock);
        transform = cmsCreateTransform(src_profile, intype, d->target_profile,
                                       outtype_intermediate | PLANAR_SH(1),
                                       intent, flags);
        if (transform && !d->float_output) {
          transform2 = cmsCreateTransform(
              d->target_profile, outtype_intermediate | PLANAR_SH(1),
              d->target_profile, outtype | PLANAR_SH(1), intent, flags);
        }
        if (!d->input_profile) {
          cmsCloseProfile(src_profile);
        }
        src_profile = nullptr;
        if (!transform) {
          throw std::runtime_error("invalid transform");
        }
        if (!d->float_output && !transform2) {
          throw std::runtime_error("invalid narrowing transform");
        }
        // Released first: reset() deletes it itself if it throws.
        cmsHTRANSFORM t1 = transform;
        transform = nullptr;
        tf.to_target.reset(t1, cmsDeleteTransform);
        if (transform2) {
          cmsHTRANSFORM t2 = transform2;
          transform2 = nullptr;
          tf.narrow.reset(t2, cmsDeleteTransform);
        }
        // Another thread may have built the same one meanwhile; keep theirs.
        auto found = d->transforms.find(key);
        if (found != d->transforms.end()) {
          tf = found->second;
        } else {
          if (d->transform_order.size() >= 8) {
            d->transforms.erase(d->transform_order.front());
            d->transform_order.erase(d->transform_order.begin());
          }
          d->transforms.emplace(key, tf);
          d->transform_order.push_back(key);
        }
      }

      cmsDoTransform(tf.to_target.get(), pixels.data(), pixels2.data(),
                     (cmsUInt32Number)npixels);

      uint8_t *out_pointer = pixels2.data();
      if (!d->float_output) {
        cmsDoTransform(tf.narrow.get(), pixels2.data(), pixels3.data(),
                       (cmsUInt32Number)npixels);
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
        throw std::runtime_error("this function should not produce 8 bit");
      }

      int alpha_err = 0;
      src_alpha = vsapi->mapGetFrame(src_props, "_Alpha", 0, &alpha_err);
      if (src_alpha) {
        dst_alpha = convert_alpha(src_alpha, d->vi.format, d->vi.width,
                                  d->vi.height, core, vsapi);
        vsapi->freeFrame(src_alpha);
        src_alpha = nullptr;
        vsapi->mapConsumeFrame(props, "_Alpha", dst_alpha, maReplace);
        dst_alpha = nullptr;
      }

      vsapi->freeFrame(src);
      return dst;
    } catch (...) {
      const std::string why = current_error();
      if (transform2) {
        cmsDeleteTransform(transform2);
      }
      if (transform) {
        cmsDeleteTransform(transform);
      }
      if (src_profile && !d->input_profile) {
        cmsCloseProfile(src_profile);
      }
      if (dst_alpha) {
        vsapi->freeFrame(dst_alpha);
      }
      if (src_alpha) {
        vsapi->freeFrame(src_alpha);
      }
      if (dst) {
        vsapi->freeFrame(dst);
      }
      vsapi->freeFrame(src);
      vsapi->setFilterError(("ConvertColor: " + why).c_str(), frameCtx);
      return nullptr;
    }
  }

  return nullptr;
}

static void VS_CC convertcolor_free(void *instanceData, VSCore *core,
                                    const VSAPI *vsapi) {
  (void)core;
  auto d = static_cast<ConvertColorData *>(instanceData);
  vsapi->freeNode(d->node);

  if (d->target_profile) {
    cmsCloseProfile(d->target_profile);
  }

  if (d->input_profile) {
    cmsCloseProfile(d->input_profile);
  }

  delete d;
}

void VS_CC convertcolor_create(const VSMap *in, VSMap *out, void *userData,
                               VSCore *core, const VSAPI *vsapi) {
  (void)userData;
  auto d = std::make_unique<ConvertColorData>();

  try {
    d->node = vsapi->mapGetNode(in, "clip", 0, nullptr);
    d->src_vi = vsapi->getVideoInfo(d->node);

    const VSVideoFormat &src_format = d->src_vi->format;
    if (src_format.colorFamily != VSColorFamily::cfRGB &&
        src_format.colorFamily != VSColorFamily::cfGray) {
      throw std::runtime_error("only RGB and GRAY input is supported");
    }
    if (src_format.sampleType == VSSampleType::stFloat) {
      if (src_format.bitsPerSample != 32) {
        throw std::runtime_error("float input must be 32 bit");
      }
    } else if (src_format.bitsPerSample != 8 &&
               src_format.bitsPerSample != 16) {
      throw std::runtime_error("integer input must be 8 or 16 bit");
    }
    if (d->src_vi->width <= 0 || d->src_vi->height <= 0) {
      throw std::runtime_error("variable-resolution input is not supported");
    }

    d->target =
        std::string(vsapi->mapGetData(in, "output_profile", 0, nullptr));

    int err = 0;

    d->float_output = !!vsapi->mapGetInt(in, "float_output", 0, &err);
    if (err) {
      d->float_output = d->src_vi->format.sampleType == VSSampleType::stFloat;
    }

    const char *input_profile_s =
        vsapi->mapGetData(in, "input_profile", 0, &err);
    if (!err) {
      std::string input_profile = std::string(input_profile_s);
      if (input_profile == "xyz") {
        d->input_profile = cmsCreateXYZProfile();
        d->input_xyz = true;
        if (d->src_vi->format.colorFamily != VSColorFamily::cfRGB) {
          throw std::runtime_error("XYZ input profile only supports RGB input");
        }
        if (d->src_vi->format.bitsPerSample == 8) {
          throw std::runtime_error("XYZ input must be 16 bit or float");
        }
      } else if (input_profile == "srgb") {
        d->input_profile = cmsCreate_sRGBProfile();
        if (d->src_vi->format.colorFamily != VSColorFamily::cfRGB) {
          throw std::runtime_error(
              "sRGB input profile only supports RGB input");
        }
      } else if (input_profile == "srgb-gray") {
        d->input_profile = create_sRGB_gray();
        if (d->src_vi->format.colorFamily != VSColorFamily::cfGray) {
          throw std::runtime_error(
              "sRGB-gray input profile only supports GRAY input");
        }
      } else {
        d->input_profile = cmsOpenProfileFromFile(input_profile.c_str(), "r");
        if (!d->input_profile) {
          throw std::runtime_error("Bad input profile");
        }
        cmsColorSpaceSignature input_color = cmsGetColorSpace(d->input_profile);
        if (input_color != cmsSigRgbData && input_color != cmsSigGrayData) {
          throw std::runtime_error("Input profile must be RGB or GRAY");
        }
        if (input_color == cmsSigRgbData &&
            d->src_vi->format.colorFamily != VSColorFamily::cfRGB) {
          throw std::runtime_error("Input profile only supports RGB input");
        }
        if (input_color == cmsSigGrayData &&
            d->src_vi->format.colorFamily != VSColorFamily::cfGray) {
          throw std::runtime_error("Input profile only supports GRAY input");
        }
      }
      if (!d->input_profile) {
        throw std::runtime_error("Bad input profile");
      }
    }

    d->vi = {
        .format = {},
        .fpsNum = d->src_vi->fpsNum,
        .fpsDen = d->src_vi->fpsDen,
        .width = d->src_vi->width,
        .height = d->src_vi->height,
        .numFrames = d->src_vi->numFrames,
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
                                 std::to_string((uint32_t)profile_colorspace));
      }
    }
    if (!d->target_profile) {
      throw std::runtime_error("Bad target profile");
    }

    if (d->target != "xyz") {
      cmsUInt32Number length = 0;
      if (!cmsSaveProfileToMem(d->target_profile, nullptr, &length) ||
          !length) {
        throw std::runtime_error("cannot serialise the target profile");
      }
      d->target_profile_bytes.resize(length);
      if (!cmsSaveProfileToMem(d->target_profile,
                               d->target_profile_bytes.data(), &length)) {
        throw std::runtime_error("cannot serialise the target profile");
      }
      d->target_profile_bytes.resize(length);
    }

    if (!vsapi->queryVideoFormat(&d->vi.format, color_family, sample_type, bits,
                                 0, 0, core)) {
      throw std::runtime_error("output format has no VapourSynth equivalent");
    }

    VSFilterDependency deps[]{{d->node, rpStrictSpatial}};
    vsapi->createVideoFilter(out, "ConvertColor", &d->vi, convertcolor_getframe,
                             convertcolor_free, fmParallel, deps, 1, d.get(),
                             core);
    d.release();
  } catch (...) {
    const std::string why = current_error();
    if (d && d->node) {
      vsapi->freeNode(d->node);
    }
    if (d && d->target_profile) {
      cmsCloseProfile(d->target_profile);
    }
    if (d && d->input_profile) {
      cmsCloseProfile(d->input_profile);
    }
    vsapi->mapSetError(out, ("ConvertColor: " + why).c_str());
  }
}

VS_EXTERNAL_API(void)
VapourSynthPluginInit2(VSPlugin *plugin, const VSPLUGINAPI *vspapi) {
  vspapi->configPlugin("moe.grass.carefulsource", "cs", "carefulsource",
                       VS_MAKE_VERSION(0, 1), VAPOURSYNTH_API_VERSION, 0,
                       plugin);
  vspapi->registerFunction("ImageSource",
                           "source:data;"
                           "yuv_to_rgb:int:opt;",
                           "clip:vnode;", imagesource_create, nullptr, plugin);
  vspapi->registerFunction("ConvertColor",
                           "clip:vnode;"
                           "output_profile:data;"
                           "input_profile:data:opt;"
                           "float_output:int:opt;",
                           "clip:vnode;", convertcolor_create, nullptr, plugin);
}
