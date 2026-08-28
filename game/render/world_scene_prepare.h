#pragma once

#include "world_animation.h"
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
// No guest state is changed here in either form.
//
// With `animation` null this is the read-only producers' entry: a sector with a live animation
// channel refuses the whole preparation, because its arrays hold geometry the frame has not
// applied yet and drawing them would silently ship stale world.
//
// With `animation` supplied the same walk instead DECODES those channels into the plan the guest
// would have written. Nothing is committed; `world_scene::animate` is the one place that does
// that, and it re-runs this in the refusing form afterwards to prove the state really advanced.
bool prepare(const world_chunk_codec::RamView &ram,
             int32_t selection,
             Prepared &out,
             const char *&why,
             world_animation::Plan *animation = nullptr);

} // namespace spyro::world_scene_prepare
