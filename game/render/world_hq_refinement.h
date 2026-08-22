#pragma once

#include "native_projection.h"
#include "world_chunk_codec.h"
#include "world_material_codec.h"
#include "world_recipe.h"

#include <array>
#include <cstdint>
#include <vector>

namespace spyro::world_hq_refinement {

// Internal contract between RenderWorldChunks' HQ classification/direct pass
// and its later medium/near refinement passes.
struct Position {
  int32_t x = 0;
  int32_t y = 0;
  int32_t z = 0;
};

struct HighVertex {
  Position position{};
  world_recipe::Vertex projected{};
  bool requiresFacingCheck = false;
};

struct Parent {
  uint32_t sector = 0;
  uint32_t source = 0;
  uint32_t ordinal = 0;
  uint32_t statusAddress = 0;
  uint32_t materialWord = 0;
  uint32_t flags = 0;
  uint16_t otBin = 0;
  uint8_t count = 0;
  uint8_t tags = 0;
  bool recheckFacing = false;
  std::array<HighVertex, 4> vertices{};
};

struct Work {
  std::vector<Parent> medium;
  std::vector<Parent> near;
  std::vector<uint8_t> status = std::vector<uint8_t>(0x200000u);
};

// Shared HQ geometry rules used while classifying roots and while projecting
// their refinement lattices. They live here so the precision reproject,
// facing, and depth semantics have one implementation.
HighVertex projectVertex(const psxport::native_projection::FixedAffine &cameraMatrix,
                         const psxport::native_projection::ProjectionParams &projection,
                         Position position,
                         uint8_t tags,
                         int clipRight);

bool facing(const std::array<HighVertex, 4> &vertices, uint32_t count, uint32_t flags);
uint32_t depthSum(const std::array<HighVertex, 4> &vertices, uint32_t count);
void applyTile(world_recipe::Face &face, const world_material_codec::DecodedTile &tile);

// Exact packed-RGB graph used by the near-quad lattice. Geometry and color
// have different interpolation graphs in the guest, so callers must not infer
// these colors by repeatedly midpointing the position lattice.
std::array<uint32_t, 25> nearQuadColorLattice(const std::array<uint32_t, 4> &corners);

bool append(const world_chunk_codec::RamView &ram,
            const psxport::native_projection::ProjectionParams &projection,
            int clipRight,
            const Work &work,
            world_recipe::Recipe &out,
            const char *&why);

} // namespace spyro::world_hq_refinement
