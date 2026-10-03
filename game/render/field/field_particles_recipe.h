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

// The type-3 arm draws the same POLY_FT4 from the same texture table as type 2, and differs only
// in where the corners go: axis-aligned in screen space from two independent half-extents, with no
// rotation and no second projection. See field_particle_quad_submitter.h.
struct SpriteQuad {
  uint32_t address = 0;
  uint32_t scanOrdinal = 0;
  uint8_t textureClass = 0;
  int16_t x = 0;
  int16_t y = 0;
  int16_t z = 0;
  uint8_t sizeX = 0;
  uint8_t sizeY = 0;
  uint8_t depthBias = 0;
  uint32_t colorCommand = 0;
  uint32_t uvClut = 0;
  uint32_t uvTpage = 0;
};

// The default arm at 0x800574F8, which every type the dispatch chain does not name — 6 and above —
// falls through to. It is the only emit-list arm that places its corners in the WORLD: one size
// byte and one angle rotate a square about the particle's own position and all four corners are
// projected, where types 2 and 3 project one centre and place their corners in screen space around
// it. See field_particle_oriented_submitter.h.
struct OrientedQuad {
  uint32_t address = 0;
  uint32_t scanOrdinal = 0;
  int16_t x = 0;
  int16_t y = 0;
  int16_t z = 0;
  uint8_t size = 0;
  // A whole sine-table entry, not a packed angle: the arm indexes D_8006CBF8 by this byte and its
  // cosine by the same byte plus 64, with no rounding step of its own.
  uint8_t angle = 0;
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
  std::vector<SpriteQuad> spriteQuads;
  std::vector<OrientedQuad> orientedQuads;
};

// Decode the reached emit-list arms: types 0, 1, 2 and 3, and every type from 6 up, which the
// guest's dispatch chain sends to one default arm. The guest renderer scans the 256-slot array from
// its base to the first type -1 terminator; g_ParticleAllocPtr is a recyclable allocation cursor,
// not the list end. Every OTHER negative type is a free hole the scan steps over, not just -2.
// Types 4 and 5 have arms of their own that are not ported yet, and refuse as one atomic scene
// layer.
Recipe derive(const world_chunk_codec::RamView &ram);
const char *statusName(Status status);

} // namespace spyro::field_particles_recipe
