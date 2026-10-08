#include "core.h"
#include "cutscene_scene_recipe.h"
#include "testutil.h"

#include <memory>

namespace {

constexpr uint32_t kCycloramaBackground = 0x80078A50u;
constexpr uint32_t kFade = 0x80075918u;
constexpr uint32_t kDrawEnvABackground = 0x80076EF9u;
constexpr uint32_t kDrawEnvBBackground = 0x80076F7Du;

void test_state_snapshot_and_preframe_apply() {
  auto core = std::make_unique<Core>();
  core->mem_w8(kCycloramaBackground + 0u, 0x12u);
  core->mem_w8(kCycloramaBackground + 1u, 0x34u);
  core->mem_w8(kCycloramaBackground + 2u, 0x56u);
  core->mem_w32(kFade, 7u);
  const auto state = spyro::cutscene_scene_recipe::read(core.get());
  core->mem_w8(kCycloramaBackground, 0u);
  spyro::cutscene_scene_recipe::prepareFrame(core.get(), state);
  CHECK_EQ(state.fade, 7u);
  CHECK_EQ(core->mem_r8(kDrawEnvABackground + 0u), 0x12u);
  CHECK_EQ(core->mem_r8(kDrawEnvABackground + 1u), 0x34u);
  CHECK_EQ(core->mem_r8(kDrawEnvABackground + 2u), 0x56u);
  CHECK_EQ(core->mem_r8(kDrawEnvBBackground + 0u), 0x12u);
  CHECK_EQ(core->mem_r8(kDrawEnvBBackground + 1u), 0x34u);
  CHECK_EQ(core->mem_r8(kDrawEnvBBackground + 2u), 0x56u);
}

void test_world_invocation() {
  auto core = std::make_unique<Core>();
  const auto invocation = spyro::cutscene_scene_recipe::worldInvocation();
  core->mem_w32(spyro::cutscene_scene_recipe::kWorldLowDetailFarLimit, 0u);
  spyro::cutscene_scene_recipe::applyWorldInvocation(core.get(), invocation);
  CHECK_EQ(invocation.worldSelection, -1);
  CHECK_EQ(invocation.lowDetailFarLimit, 0x14000u);
  CHECK_EQ(core->mem_r32(spyro::cutscene_scene_recipe::kWorldLowDetailFarLimit), 0x14000u);
}

} // namespace

int main() {
  RUN(state_snapshot_and_preframe_apply);
  RUN(world_invocation);
  return pt_summary();
}
