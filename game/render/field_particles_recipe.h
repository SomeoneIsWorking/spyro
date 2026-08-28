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

struct TexturedQuad {
  uint32_t address = 0;
  uint8_t textureClass = 0;
  int16_t x = 0;
  int16_t y = 0;
  int16_t z = 0;
  uint8_t size = 0;
  uint16_t angle = 0;
  uint8_t depthBias = 0;
  uint32_t colorCommand = 0;
  uint32_t uvClut = 0;
  uint32_t uvTpage = 0;
};

struct Recipe {
  Status status = Status::ValidEmpty;
  const char *refusal = "none";
  uint32_t records = 0;
  std::vector<Point> points;
  std::vector<TexturedQuad> texturedQuads;
};

// Decode the reached type-0/type-2 emit-list arms. The guest renderer scans the 256-slot array from
// its base to the first type -1 terminator; g_ParticleAllocPtr is a recyclable allocation cursor,
// not the list end. Type -2 slots are free holes. Other particle types refuse as one atomic scene
// layer until their retained ASM has been ported.
Recipe derive(const world_chunk_codec::RamView &ram);
const char *statusName(Status status);

} // namespace spyro::field_particles_recipe
