#include "field_scene_recipe.h"
#include "testutil.h"

namespace {

using spyro::field_scene_recipe::Layer;
using spyro::field_scene_recipe::State;

void test_visible_player_and_fade_preserve_authored_order() {
  State state{};
  state.environment = {.cameraOcclusionGroup = 2, .occlusionGroupCount = 4, .stage = 0};
  state.playerVisible = true;
  state.fade = 3;
  const auto plan = spyro::field_scene_recipe::derive(state);
  CHECK_EQ(plan.environment.worldSelection, 2);
  CHECK_EQ(plan.environment.cullingDistance, 0x28000u);
  CHECK_EQ(plan.layerCount, 7u);
  CHECK(plan.layers[0] == Layer::MobyListBuild);
  CHECK(plan.layers[1] == Layer::Collectables);
  CHECK(plan.layers[2] == Layer::RegularActors);
  CHECK(plan.layers[3] == Layer::PlayerActor);
  CHECK(plan.layers[4] == Layer::Environment);
  CHECK(plan.layers[5] == Layer::Cyclorama);
  CHECK(plan.layers[6] == Layer::Fade);
}

void test_hidden_player_and_absent_fade_are_valid_empty_layers() {
  State state{};
  state.environment = {.cameraOcclusionGroup = 7, .occlusionGroupCount = 4, .stage = 0};
  state.flightLevel = true;
  const auto plan = spyro::field_scene_recipe::derive(state);
  CHECK_EQ(plan.environment.worldSelection, -1);
  CHECK_EQ(plan.environment.cullingDistance, 0x14000u);
  CHECK_EQ(plan.layerCount, 4u);
  CHECK(plan.layers[0] == Layer::MobyListBuild);
  CHECK(plan.layers[1] == Layer::RegularActors);
  CHECK(plan.layers[2] == Layer::Environment);
  CHECK(plan.layers[3] == Layer::Cyclorama);
}

} // namespace

int main() {
  RUN(visible_player_and_fade_preserve_authored_order);
  RUN(hidden_player_and_absent_fade_are_valid_empty_layers);
  return pt_summary();
}
