#include "core.h"
#include "stage13_scene_recipe.h"
#include "testutil.h"

#include <memory>

namespace {

void test_title_and_attract_split() {
  CHECK(spyro::stage13_scene_recipe::hasSharedBackdrop(0));
  CHECK(spyro::stage13_scene_recipe::hasSharedBackdrop(1));
  CHECK(spyro::stage13_scene_recipe::hasSharedBackdrop(2));
  CHECK(!spyro::stage13_scene_recipe::hasSharedBackdrop(3));
}

void test_title_world_invocation_state() {
  auto core = std::make_unique<Core>();
  const auto invocation = spyro::stage13_scene_recipe::sharedBackdropInvocation();
  core->mem_w32(spyro::stage13_scene_recipe::kWorldLowDetailFarLimit, 0);
  spyro::stage13_scene_recipe::apply(core.get(), invocation);
  CHECK_EQ(invocation.worldSelection, -1);
  CHECK_EQ(core->mem_r32(spyro::stage13_scene_recipe::kWorldLowDetailFarLimit),
           invocation.lowDetailFarLimit);
  CHECK(invocation.lowDetailFarLimit != 0u);
}

} // namespace

int main() {
  RUN(title_and_attract_split);
  RUN(title_world_invocation_state);
  return pt_summary();
}
