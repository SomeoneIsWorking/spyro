#pragma once

#include <cstdint>

class Core;

namespace spyro::stage13_scene_recipe {

constexpr uint32_t kTitleModeAttract = 3u;
constexpr uint32_t kWorldLowDetailFarLimit = 0x800785d0u;

struct BackdropInvocation {
  int32_t worldSelection = -1;
  uint32_t lowDetailFarLimit = 0;
};

// The title overlay handler (modes 0..2) owns a shared actor/world/cyclorama
// tail. Attract mode 3 dispatches a different handler with no such tail.
bool hasSharedBackdrop(uint32_t titleMode);

// Exact render-only input written by the resident title overlay immediately
// before its RenderWorldChunks call.
BackdropInvocation sharedBackdropInvocation();
void apply(Core *core, const BackdropInvocation &invocation);

} // namespace spyro::stage13_scene_recipe
