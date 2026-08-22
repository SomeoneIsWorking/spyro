#pragma once

#include "native_projection.h"
#include "world_chunk_codec.h"
#include "world_recipe.h"
#include "world_scene_prepare.h"

#include <array>
#include <cstdint>
#include <vector>

namespace spyro::world_hq_recipe {

enum class Decision : uint8_t { CommonClip, Backface, DepthRejected, Direct, Medium, Near };

struct AuditVertex {
  int16_t modelX = 0;
  int16_t modelY = 0;
  int16_t modelZ = 0;
  int16_t sx = 0;
  int16_t sy = 0;
  uint16_t sz = 0;
};

struct AuditEntry {
  uint32_t source = 0;
  uint32_t sector = 0;
  uint32_t flags = 0;
  uint32_t depth = 0;
  uint8_t tags = 0;
  uint8_t chunkCommonClip = 0;
  uint8_t commonClip = 0;
  Decision decision = Decision::DepthRejected;
  std::array<AuditVertex, 4> vertices{};
  psxport::native_projection::FixedAffine cameraMatrix{};
  psxport::native_projection::ProjectionParams projection{};
};

using Audit = std::vector<AuditEntry>;

// Append RenderWorldChunks' high-detail direct and subdivision faces selected
// by phase 1. The caller supplies the current projection and exclusive right
// clip boundary, so this module has no Core, GPU, packet-pool, ordering-table,
// scratchpad, or ambient GTE dependency.
bool append(const world_chunk_codec::RamView &ram,
            const world_scene_prepare::Prepared &prepared,
            const psxport::native_projection::ProjectionParams &projection,
            int clipRight,
            world_recipe::Recipe &out,
            const char *&why,
            Audit *audit = nullptr);

} // namespace spyro::world_hq_recipe
