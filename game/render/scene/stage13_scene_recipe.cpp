#include "stage13_scene_recipe.h"

#include "core.h"

namespace spyro::stage13_scene_recipe {
namespace {

// OV_5B800 0x8007DCC0..0x8007DCD4 constructs 0x0001C000, stores it at
// 0x800785D0, then calls RenderWorldChunks(-1). This is the selected stage-13
// overlay's authored culling distance, not the different 0x14000 value in the
// main executable's cutscene handler.
constexpr uint32_t kTitleLowDetailFarLimit = 0x0001c000u;

} // namespace

bool hasSharedBackdrop(uint32_t titleMode) {
  return titleMode != kTitleModeAttract;
}

BackdropInvocation sharedBackdropInvocation() {
  return {.worldSelection = -1, .lowDetailFarLimit = kTitleLowDetailFarLimit};
}

void apply(Core *core, const BackdropInvocation &invocation) {
  core->mem_w32(kWorldLowDetailFarLimit, invocation.lowDetailFarLimit);
}

} // namespace spyro::stage13_scene_recipe
