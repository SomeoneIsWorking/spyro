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
  // Position in the guest's single scan of the emit list. The three arms interleave in that scan
  // and are linked into the ordering table from it, so the draw order between a point, a line and a
  // textured quad is this number and not the order of the three lists below.
  uint32_t scanOrdinal = 0;
  int16_t x = 0;
  int16_t y = 0;
  int16_t z = 0;
  uint8_t depthBias = 0;
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;
};

// The type-1 arm projects two independent world endpoints and emits one LINE_G2 between them. Only
// the first endpoint is clipped and depth-sorted; the second only supplies its screen position and
// its own vertex colour, whose unused command byte carries the shared depth bias.
struct Line {
  uint32_t address = 0;
  uint32_t scanOrdinal = 0;
  int16_t x0 = 0;
  int16_t y0 = 0;
  int16_t z0 = 0;
  int16_t x1 = 0;
  int16_t y1 = 0;
  int16_t z1 = 0;
  uint8_t depthBias = 0;
  uint8_t r0 = 0;
  uint8_t g0 = 0;
  uint8_t b0 = 0;
  uint8_t r1 = 0;
  uint8_t g1 = 0;
  uint8_t b1 = 0;
};

struct TexturedQuad {
  uint32_t address = 0;
  uint32_t scanOrdinal = 0;
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
  // A refusal that only says "particle_type" cannot tell an unported arm from a decode fault, and
  // this list is scanned live, so the offending slot is gone by the time anyone looks. Both are
  // carried out with the refusal; they are 0/-1 on every non-refusing path.
  int32_t refusedType = -1;
  uint32_t refusedAddress = 0;
  std::vector<Point> points;
  std::vector<Line> lines;
  std::vector<TexturedQuad> texturedQuads;
};

// Decode the reached type-0/type-1/type-2 emit-list arms. The guest renderer scans the 256-slot
// array from its base to the first type -1 terminator; g_ParticleAllocPtr is a recyclable
// allocation cursor, not the list end. Type -2 slots are free holes. Other particle types refuse as
// one atomic scene layer until their retained ASM has been ported.
Recipe derive(const world_chunk_codec::RamView &ram);
const char *statusName(Status status);

} // namespace spyro::field_particles_recipe
