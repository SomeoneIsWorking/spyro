#include "stage13_scene_recipe.h"

#include "core.h"

namespace spyro::stage13_scene_recipe {
namespace {

// The stage-13 overlay's authored culling distance, distinct from the cutscene handler's 0x14000.
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
