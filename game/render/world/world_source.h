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
  std::vector<GuestAddressRange> resourceRanges;
};

Selection select(const world_chunk_codec::RamView &ram, int32_t group);

struct MaterialDifference {
  bool layoutMismatch = false;
  uint32_t scannedBlocks = 0;
  uint32_t scannedBytes = 0;
  uint32_t changedBytes = 0;
  uint32_t blockAddress = 0;
  uint32_t blockSize = 0;
  uint32_t firstAddress = 0;
  uint8_t before = 0;
  uint8_t after = 0;
};

// Only the authored world texture records and subdivision tables are retained. Addresses remain
// provenance for the existing packed-table codec; this owns no general guest address space.
class Materials {
public:
  static Materials capture(const world_chunk_codec::RamView &ram);
  bool operator==(const Materials &) const = default;
  // Discrete UV state may advance between geometry samples. Layout, residency spans and every
  // non-UV authored byte (including refinement tables) must retain the same meaning.
  bool sameIdentity(const Materials &other) const;
  MaterialDifference difference(const Materials &other) const;
  std::vector<GuestAddressRange> resourceRanges() const;
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
    bool operator==(const Block &) const = default;
  };
  const Block *find(uint32_t address, uint32_t size) const;
  bool isTileUvByte(uint32_t address) const;
  std::vector<Block> blocks_;
  std::vector<uint32_t> tilePairs_;
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

  // Exact physical authored-input spans; excludes mutable camera/environment globals. Sorted
  // and deduplicated without joining neighboring resources across residency boundaries.
  std::vector<GuestAddressRange> resourceRanges() const;
};

Source capture(const world_chunk_codec::RamView &ram,
               int32_t selection,
               psxport::native_projection::ProjectionParams projection,
               int32_t clipRight,
               std::optional<uint32_t> cullingDistance = std::nullopt);

} // namespace spyro::world_source
