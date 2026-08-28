#pragma once

#include "native_projection.h"
#include "world_chunk_codec.h"
#include "world_recipe.h"
#include "world_scene_prepare.h"

namespace spyro::world_lq_recipe {

// Append the low-detail RenderWorldChunks faces selected by phase 1. The
// caller owns display policy (including widescreen) and passes its exclusive
// right clipping boundary explicitly; this builder has no Core, GPU, packet
// pool, ordering-table, or ambient GTE dependency.
bool append(const world_chunk_codec::RamView &ram,
            const world_scene_prepare::Prepared &prepared,
            const psxport::native_projection::ProjectionParams &projection,
            int clipRight,
            uint32_t farLimit,
            world_recipe::Recipe &out,
            const char *&why);

} // namespace spyro::world_lq_recipe
