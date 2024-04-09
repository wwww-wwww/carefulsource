#include "carefulsource.h"

#include "decoder_png.h"
#include <iostream>
#include <memory>

static const VSFrame *VS_CC imagesource_getframe(
    int n, int activationReason, void *instanceData, void **frameData,
    VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
  auto d = static_cast<ImageSourceData *>(instanceData);

  if (activationReason == arInitial) {
    VSFrame *dst = vsapi->newVideoFrame(&d->vi.format, d->vi.width,
                                        d->vi.height, nullptr, core);

    uint8_t *planes[3] = {};
    ptrdiff_t stride[3] = {};

    VSFrame *dst_alpha = nullptr;
    uint8_t *plane_alpha = nullptr;
    ptrdiff_t stride_alpha;

    for (int p = 0; p < d->vi.format.numPlanes; p++) {
      planes[p] = vsapi->getWritePtr(dst, p);
      stride[p] = vsapi->getStride(dst, p);
    }

    VSVideoFormat format = {};
    vsapi->queryVideoFormat(&format, d->vi.format.colorFamily,
                            d->vi.format.sampleType, d->vi.format.bitsPerSample,
                            d->vi.format.subSamplingW,
                            d->vi.format.subSamplingH, core);

    ImageInfo info = d->decoder->info;

    if (info.has_alpha) {
      VSVideoFormat format_alpha = {};
      vsapi->queryVideoFormat(&format_alpha, VSColorFamily::cfGray,
                              format.sampleType, format.bitsPerSample, 0, 0,
                              core);

      dst_alpha = vsapi->newVideoFrame(&format_alpha, d->vi.width, d->vi.height,
                                       nullptr, core);
      plane_alpha = vsapi->getWritePtr(dst_alpha, 0);
      stride_alpha = vsapi->getStride(dst_alpha, 0);

      vsapi->mapSetInt(vsapi->getFramePropertiesRW(dst_alpha), "_ColorRange", 0,
                       maAppend);
    }

    uint32_t sstride = d->vi.width * info.components;

    std::vector<uint8_t> pixels = d->decoder->decode();

    uint8_t *ppixels = pixels.data();
    for (uint32_t y = 0; y < d->vi.height; y++) {
      for (uint32_t x = 0; x < d->vi.width; x++) {
        if (d->vi.format.numPlanes == 3) {
          planes[0][y * stride[0] + x] = ppixels[x * info.components + 0];
          planes[1][y * stride[1] + x] = ppixels[x * info.components + 1];
          planes[2][y * stride[2] + x] = ppixels[x * info.components + 2];
        }
        if (dst_alpha) {
          plane_alpha[y * stride_alpha + x] = ppixels[x * info.components + 2];
        }
      }
      ppixels += sstride;
    }

    VSMap *Props = vsapi->getFramePropertiesRW(dst);

    if (dst_alpha)
      vsapi->mapConsumeFrame(Props, "_Alpha", dst_alpha, maAppend);

    vsapi->mapSetInt(Props, "_ColorRange", 0, maAppend);
    vsapi->mapSetInt(Props, "_Matrix", 0, maAppend);
    vsapi->mapSetInt(Props, "_Primaries", 1, maAppend);
    vsapi->mapSetInt(Props, "_Transfer", 13, maAppend);

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

  vsapi->queryVideoFormat(&d->vi.format, info.color, info.sample_type,
                          info.bits, info.subsampling_w, info.subsampling_h,
                          core);
  d->vi.width = info.width;
  d->vi.height = info.height;
  d->vi.numFrames = 1;
  d->vi.fpsNum = 1;
  d->vi.fpsDen = 1;

  vsapi->createVideoFilter(out, "ImageSource", &d->vi, imagesource_getframe,
                           imagesource_free, fmUnordered, nullptr, 0, d, core);
}

VS_EXTERNAL_API(void)
VapourSynthPluginInit2(VSPlugin *plugin, const VSPLUGINAPI *vspapi) {
  vspapi->configPlugin("moe.grass.carefulsource", "cs", "carefulsource",
                       VS_MAKE_VERSION(1, 0), VAPOURSYNTH_API_VERSION, 0,
                       plugin);
  vspapi->registerFunction("ImageSource", "source:data;", "clip:vnode;",
                           imagesource_create, nullptr, plugin);
}
