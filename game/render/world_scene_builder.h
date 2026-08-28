#pragma once

#include "world_hq_recipe.h"
#include "world_recipe.h"

#include <optional>

#include <cstdint>

class Core;

namespace spyro::world_scene {

// Advances RenderWorldChunks' phase-1 animation channels for this call's selection: the ONE place
// in the native world path that writes guest state. The guest does this inside the renderer, and
// phase 2 projects the arrays it just wrote, so it has to happen before `build` reads them.
//
// Returns false and leaves guest RAM untouched when the animation data does not decode; the plan
// is committed whole or not at all. After committing, the selection is re-walked in its refusing
// form, so a channel that somehow survived is reported rather than assumed away.
struct AnimationResult {
  bool ok = false;
  const char *refusal = "none";
  uint32_t channels = 0;
  uint32_t direct = 0;
  uint32_t blended = 0;
  uint32_t writes = 0;
  uint32_t lastAddress = 0; // an address the plan actually wrote — the oracle's mutation probe
};
AnimationResult animate(Core *core, int32_t selection);

// Builds a complete immutable RenderWorldChunks recipe. The builder reads
// current game state only; it never executes guest code and never reads the
// guest packet pool, ordering table, scratchpad, or GTE output registers.
world_recipe::Recipe build(Core *core,
                           int32_t selection,
                           world_hq_recipe::Audit *audit = nullptr,
                           std::optional<uint32_t> cullingDistance = std::nullopt);

} // namespace spyro::world_scene
