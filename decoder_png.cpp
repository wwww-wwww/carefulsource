#include "decoder_png.h"

#include <algorithm>
#include <iostream>

PngDecoder::PngDecoder(std::string path) : BaseDecoder(path) {
  reader.file.open(path, std::ios_base::binary);
  reader.file.unsetf(std::ios::skipws);
  reader.file.seekg(0, std::ios::end);
  reader.size = reader.file.tellg();

  initialize();

  info = {
      .width = png_get_image_width(png, pinfo),
      .height = png_get_image_height(png, pinfo),
  };
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

  finished_reading = false;
}

std::vector<uint8_t> PngDecoder::decode() {
  if (finished_reading)
    initialize();

  std::vector<uint8_t> pixels(info.height * info.width * 4);

  std::vector<png_bytep> row_pointers(info.height);
  for (uint32_t y = 0; y < info.height; y++) {
    row_pointers[y] = pixels.data() + (y * info.width * 4);
  }

  for (uint32_t x = 0; x < pixels.size(); x++)
    pixels[x] = x;

  png_read_image(png, row_pointers.data());

  finished_reading = true;

  return pixels;
}
