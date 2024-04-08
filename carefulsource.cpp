#include "carefulsource.h"

#include <fstream>
#include <iostream>
#include <iterator>
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

    for (int p = 0; p < d->vi.format.numPlanes; p++) {
      planes[p] = vsapi->getWritePtr(dst, p);
      stride[p] = vsapi->getStride(dst, p);
      std::cout << "stride " << p << " " << stride[p] << std::endl;
    }

    VSVideoFormat format = {};
    vsapi->queryVideoFormat(&format, VSColorFamily::cfRGB,
                            VSSampleType::stInteger, 8, 0, 0, core);
    VSVideoFormat format_alpha = {};
    vsapi->queryVideoFormat(&format_alpha, VSColorFamily::cfGray,
                            format.sampleType, format.bitsPerSample, 0, 0,
                            core);

    VSFrame *dst_alpha = vsapi->newVideoFrame(&format_alpha, d->vi.width,
                                              d->vi.height, nullptr, core);
    uint8_t *plane_alpha = vsapi->getWritePtr(dst_alpha, 0);
    ptrdiff_t stride_alpha = vsapi->getStride(dst_alpha, 0);

    std::cout << "stride_alpha " << stride_alpha << std::endl;

    vsapi->mapSetInt(vsapi->getFramePropertiesRW(dst_alpha), "_ColorRange", 0,
                     maAppend);

    std::cout << "decode " << d->file_path << std::endl;

    auto errorFn = [](png_struct *, png_const_charp msg) {
      throw std::runtime_error(msg);
    };

    auto warnFn = [](png_struct *, png_const_charp msg) {
      std::cout << msg << std::endl;
    };

    png_struct *png;
    png_info *pinfo;
    PngReader reader;
    std::vector<uint8_t> buffer;

    png =
        png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, errorFn, warnFn);
    if (!png) {
      throw std::runtime_error("Failed to create png read struct");
    }
    pinfo = png_create_info_struct(png);
    if (!pinfo) {
      throw std::runtime_error("Failed to create png info struct");
    }

    std::ifstream file(d->file_path, std::ios_base::binary);
    file.unsetf(std::ios::skipws);

    file.seekg(0, std::ios::end);
    size_t filesize = file.tellg();
    file.seekg(0, std::ios::beg);

    std::cout << "filesize " << filesize << std::endl;

    buffer.reserve(filesize);

    buffer.insert(buffer.begin(), std::istream_iterator<uint8_t>(file),
                  std::istream_iterator<uint8_t>());

    reader = {.bytes = buffer.data(), .read = 0, .remain = buffer.size()};

    auto readFn = [](png_struct *p, png_byte *data, png_size_t length) {
      auto *r = (PngReader *)png_get_io_ptr(p);
      uint32_t next = std::min(r->remain, (uint32_t)length);
      if (next > 0) {
        memcpy(data, r->bytes + r->read, next);
        r->read += next;
        r->remain -= next;
      }
    };

    png_set_read_fn(png, &reader, readFn);
    png_read_info(png, pinfo);

    uint32_t width = d->vi.width;
    uint32_t height = d->vi.height;
    uint32_t sstride = width * 4;

    std::vector<uint8_t> pixels(width * height * 4);

    int32_t passes = png_set_interlace_handling(png);
    while (--passes >= 0) {
      uint8_t *ppixels = pixels.data();
      for (uint32_t y = 0; y < height; y++) {
        png_read_row(png, ppixels, nullptr);

        for (uint32_t x = 0; x < width; x++) {
          planes[0][y * stride[0] + x] = ppixels[x * 4 + 0];
          planes[1][y * stride[1] + x] = ppixels[x * 4 + 1];
          planes[2][y * stride[2] + x] = ppixels[x * 4 + 2];
          plane_alpha[y * stride_alpha + x] = ppixels[x * 4 + 2];
        }
        ppixels += sstride;
      }
    }

    VSMap *Props = vsapi->getFramePropertiesRW(dst);
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
  d->file_path = file_path;
  std::cout << "path: " << d->file_path << std::endl;

  auto errorFn = [](png_struct *, png_const_charp msg) {
    throw std::runtime_error(msg);
  };

  auto warnFn = [](png_struct *, png_const_charp msg) {
    std::cout << msg << std::endl;
  };

  png_struct *png;
  png_info *pinfo;
  PngReader reader;
  std::vector<uint8_t> buffer;

  png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, errorFn, warnFn);
  if (!png) {
    throw std::runtime_error("Failed to create png read struct");
  }
  pinfo = png_create_info_struct(png);
  if (!pinfo) {
    throw std::runtime_error("Failed to create png info struct");
  }

  std::ifstream file(d->file_path, std::ios_base::binary);
  file.unsetf(std::ios::skipws);

  file.seekg(0, std::ios::end);
  size_t filesize = file.tellg();
  file.seekg(0, std::ios::beg);

  buffer.reserve(filesize);

  std::cout << "filesize " << filesize << std::endl;

  buffer.insert(buffer.begin(), std::istream_iterator<uint8_t>(file),
                std::istream_iterator<uint8_t>());

  reader = {.bytes = buffer.data(), .read = 0, .remain = buffer.size()};

  auto readFn = [](png_struct *p, png_byte *data, png_size_t length) {
    auto *r = (PngReader *)png_get_io_ptr(p);
    uint32_t next = std::min(r->remain, (uint32_t)length);
    if (next > 0) {
      memcpy(data, r->bytes + r->read, next);
      r->read += next;
      r->remain -= next;
    }
  };

  png_set_read_fn(png, &reader, readFn);
  png_read_info(png, pinfo);

  uint32_t width = png_get_image_width(png, pinfo);
  uint32_t height = png_get_image_height(png, pinfo);

  std::cout << "width " << width << " height " << height << std::endl;

  vsapi->queryVideoFormat(&d->vi.format, VSColorFamily::cfRGB,
                          VSSampleType::stInteger, 8, 0, 0, core);
  d->vi.width = width;
  d->vi.height = height;
  d->vi.numFrames = 1;
  d->vi.fpsNum = 1;
  d->vi.fpsDen = 1;

  uint32_t components = png_get_channels(png, pinfo);
  std::cout << "components " << components << std::endl;

  uint8_t colorType = png_get_color_type(png, pinfo);
  uint8_t bitDepth = png_get_bit_depth(png, pinfo);

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
