#pragma once

#include "world_chunk_codec.h"

#include <cstdint>
#include <vector>

namespace spyro::field_particles_recipe {

enum class Status : uint8_t {
  Ready,
  ValidEmpty,
  InvalidPointers,
  UnsupportedType,
};

struct Point {
  uint32_t address = 0;
  int16_t x = 0;
  int16_t y = 0;
  int16_t z = 0;
  uint8_t depthBias = 0;
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;
};

struct Recipe {
  Status status = Status::ValidEmpty;
  const char *refusal = "none";
  uint32_t records = 0;
  std::vector<Point> points;
};

// Decode the reached type-0 emit-list arm. The list pointers and 0x20-byte record stride are the
// guest's own state; no guessed particle count or fixed world position is accepted here. Other
// particle types refuse as one atomic scene layer until their retained ASM has been ported.
Recipe derive(const world_chunk_codec::RamView &ram);
const char *statusName(Status status);

} // namespace spyro::field_particles_recipe
