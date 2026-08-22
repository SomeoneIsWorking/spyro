#include "world_chunk_codec.h"

#include <cstdlib>
#include <vector>

using namespace spyro::world_chunk_codec;

namespace {

void w32(std::vector<uint8_t> &ram, uint32_t address, uint32_t value) {
  for (uint32_t i = 0; i < 4; ++i) {
    ram[address + i] = (uint8_t)(value >> (i * 8u));
  }
}

void require(bool value) {
  if (!value) {
    std::abort();
  }
}

} // namespace

int main() {
  std::vector<uint8_t> bytes(0x1000);
  RamView ram(bytes);
  constexpr uint32_t low = 0x100;
  w32(bytes, low + 8, 0x11223344u);
  w32(bytes, low + 0x0c, 0x55667788u);
  w32(bytes, low + 0x10, 2u | (3u << 8) | (1u << 16));
  w32(bytes, low + 0x1c, 0xaaaaaaaau);
  w32(bytes, low + 0x20, 0xbbbbbbbbu);
  w32(bytes, low + 0x24, 1u);
  w32(bytes, low + 0x28, 2u);
  w32(bytes, low + 0x2c, 3u);
  w32(bytes, low + 0x30, 0x12345678u);
  w32(bytes, low + 0x34, 0x9abcdef0u);
  LowChunk decodedLow{};
  require(decodeLow(ram, low, decodedLow) == Status::Ok);
  require(decodedLow.vertices.size() == 2 && decodedLow.colors.size() == 3 &&
          decodedLow.faces.size() == 1 && decodedLow.faces[0].address == low + 0x30);

  constexpr uint32_t high = 0x400;
  // vertex offset 0, four vertices, plane stride 16, one 16-byte face.
  w32(bytes, high + 0x14, 4u | (16u << 6) | (16u << 12));
  for (uint32_t i = 0; i < 16; ++i) {
    w32(bytes, high + 0x1c + i * 4u, i + 1u);
  }
  HighChunk decodedHigh{};
  require(decodeHigh(ram, high, decodedHigh) == Status::Ok);
  require(decodedHigh.vertices.size() == 4 && decodedHigh.farColors.size() == 4 &&
          decodedHigh.nearColors.size() == 4 && decodedHigh.faces.size() == 1);

  // The on-disc count is an unsigned byte; 255 is the largest valid value,
  // not a sentinel. Span validation, rather than an invented smaller cap,
  // decides whether its payload is safe.
  w32(bytes, high + 0x14, 0xffu);
  require(decodeHigh(ram, high, decodedHigh) == Status::Ok);
  require(decodedHigh.vertices.size() == 255);

  HighChunk rejected{};
  w32(bytes, high + 0x14, 0xffu | (0x3fcu << 6) | (0xff0u << 12));
  require(decodeHigh(ram, high, rejected) != Status::Ok);
  return 0;
}
