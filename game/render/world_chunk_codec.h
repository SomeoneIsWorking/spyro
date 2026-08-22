#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace spyro::world_chunk_codec {

class RamView {
public:
  explicit RamView(std::span<const uint8_t> bytes) : mBytes(bytes) {}
  bool contains(uint32_t address, uint32_t size) const;
  uint8_t r8(uint32_t address) const;
  uint16_t r16(uint32_t address) const;
  uint32_t r32(uint32_t address) const;

private:
  std::span<const uint8_t> mBytes;
};

struct LowFace {
  uint32_t address = 0;
  uint32_t vertexWord = 0;
  uint32_t materialWord = 0;
};

struct LowChunk {
  uint32_t address = 0;
  uint32_t originWord = 0;
  uint16_t originZ = 0;
  uint32_t descriptor = 0;
  std::vector<uint32_t> vertices;
  std::vector<uint32_t> colors;
  std::vector<LowFace> faces;
};

struct HighFace {
  uint32_t address = 0;
  uint32_t vertexWord = 0;
  uint32_t colorWord = 0;
  uint32_t materialWord = 0;
  uint32_t flags = 0;
};

struct HighChunk {
  uint32_t address = 0;
  uint32_t originWord = 0;
  uint32_t originAndOffset = 0;
  uint32_t layout = 0;
  std::vector<uint32_t> vertices;
  std::vector<uint32_t> farColors;
  std::vector<uint32_t> nearColors;
  std::vector<HighFace> faces;
};

enum class Status : uint8_t { Ok, HeaderBounds, LayoutOverflow, PayloadBounds, InvalidCount };

Status decodeLow(const RamView &ram, uint32_t address, LowChunk &out);
Status decodeHigh(const RamView &ram, uint32_t address, HighChunk &out);

} // namespace spyro::world_chunk_codec
