#include "decoder_jpegli.h"
#include "lib/extras/dec/jpegli.h"
#include <iostream>

JpegliDecoder::JpegliDecoder(std::vector<uint8_t> *data, bool rgb)
    : BaseDecoder(data) {
  std::cout << "JpegliDecoder::JpegliDecoder" << std::endl;
}

std::vector<uint8_t> JpegliDecoder::decode() {
  std::cout << "JpegliDecoder::decode" << std::endl;
  return {};
}
