#pragma once

#include "world_hq_recipe.h"
#include "world_recipe.h"

#include <cstdint>

class Core;

namespace spyro::world_scene {

// Builds a complete immutable RenderWorldChunks recipe. The builder reads
// current game state only; it never executes guest code and never reads the
// guest packet pool, ordering table, scratchpad, or GTE output registers.
world_recipe::Recipe build(Core *core, int32_t selection, world_hq_recipe::Audit *audit = nullptr);

} // namespace spyro::world_scene
