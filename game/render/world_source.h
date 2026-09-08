#pragma once

#include "native_projection.h"
#include "world_chunk_codec.h"

#include <array>
#include <optional>
#include <vector>

namespace spyro::world_source {

struct Camera {
  psxport::native_projection::FixedAffine projectionMatrix{};
  psxport::native_projection::FixedAffine cullingMatrix{};
  std::array<int32_t, 3> position{};
};

struct SectorHeader {
  uint32_t address = 0;
  uint32_t center = 0;
  uint32_t extent = 0;
  uint32_t animation = 0;
};

// Selection occurrences retain authored order and duplicates, before any camera/LOD rejection.
struct Selection {
  bool valid = false;
  const char *refusal = "uncaptured";
  int32_t group = -1;
  Camera camera{};
  uint32_t lodDistance = 0;
  uint32_t cullingDistance = 0;
  bool skipLow = false;
  std::vector<uint8_t> occurrences;
  std::array<std::optional<SectorHeader>, 256> sectors{};
};

Selection select(const world_chunk_codec::RamView &ram, int32_t group);

// Only the authored world texture records and subdivision tables are retained. Addresses remain
// provenance for the existing packed-table codec; this owns no general guest address space.
class Materials {
public:
  static Materials capture(const world_chunk_codec::RamView &ram);
  bool contains(uint32_t address, uint32_t size) const;
  uint8_t r8(uint32_t address) const;
  uint16_t r16(uint32_t address) const;
  uint32_t r32(uint32_t address) const;
  uint32_t count() const {
    return count_;
  }
  uint32_t lowBase() const {
    return lowBase_;
  }
  uint32_t highBase() const {
    return highBase_;
  }

private:
  struct Block {
    uint32_t address;
    std::vector<uint8_t> bytes;
  };
  const Block *find(uint32_t address, uint32_t size) const;
  std::vector<Block> blocks_;
  uint32_t count_ = 0;
  uint32_t lowBase_ = 0;
  uint32_t highBase_ = 0;
};

struct Sector {
  world_chunk_codec::Status lowStatus = world_chunk_codec::Status::HeaderBounds;
  world_chunk_codec::Status highStatus = world_chunk_codec::Status::HeaderBounds;
  world_chunk_codec::LowChunk low{};
  world_chunk_codec::HighChunk high{};
};

// Owned, unprojected endpoint input. Builders take const Source and never revisit live RAM.
struct Source {
  Selection selection{};
  psxport::native_projection::ProjectionParams projection{};
  int32_t clipRight = 512;
  Materials materials{};
  std::array<std::optional<Sector>, 256> sectors{};
};

Source capture(const world_chunk_codec::RamView &ram,
               int32_t selection,
               psxport::native_projection::ProjectionParams projection,
               int32_t clipRight,
               std::optional<uint32_t> cullingDistance = std::nullopt);

} // namespace spyro::world_source
