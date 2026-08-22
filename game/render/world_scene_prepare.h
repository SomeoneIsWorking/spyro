#pragma once

#include "world_chunk_codec.h"

#include <array>
#include <cstdint>
#include <vector>

namespace spyro::world_scene_prepare {

struct TaggedSector {
  uint32_t address = 0;
  uint8_t index = 0;
  uint8_t tags = 0;
};

struct Prepared {
  std::array<uint8_t, 256> broadVisible{};
  std::vector<TaggedSector> low;
  std::vector<TaggedSector> high;
  uint32_t selectedSectors = 0;
};

// Builds RenderWorldChunks' phase-1 sector lists from an immutable RAM view.
// No guest state is changed; unsupported active animations reject the whole
// preparation before a caller can submit a partial scene.
bool prepare(const world_chunk_codec::RamView &ram,
             int32_t selection,
             Prepared &out,
             const char *&why);

} // namespace spyro::world_scene_prepare
