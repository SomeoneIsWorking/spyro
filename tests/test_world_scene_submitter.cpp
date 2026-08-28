#include "core.h"
#include "game.h"
#include "render_queue.h"
#include "testutil.h"
#include "world_scene_submitter.h"

#include <memory>

namespace {

spyro::world_recipe::Recipe readyRecipe() {
  spyro::world_recipe::Recipe recipe{};
  recipe.status = spyro::world_recipe::Status::Ready;
  recipe.faces.push_back({.family = spyro::world_recipe::Family::G3,
                          .vertexCount = 3,
                          .otBin = 12,
                          .paintGroup = 0,
                          .material = {.textured = false}});
  return recipe;
}

struct Harness {
  std::unique_ptr<Core> core = std::make_unique<Core>();
  std::unique_ptr<Game> game = std::make_unique<Game>();

  Harness() {
    core->game = game.get();
  }
};

void test_ready_recipe_preflights_without_mutation() {
  Harness harness;
  const auto plan = spyro::world_scene_submitter::prepare(
      harness.core.get(), harness.game->rq, 0x800258f0u, readyRecipe());
  CHECK(plan.status == spyro::world_scene_submitter::Status::Ready);
  CHECK_EQ(plan.paintOrder.size(), 1u);
  CHECK_EQ(harness.game->rq.n, 0);
}

void test_invalid_material_refuses_atomically() {
  Harness harness;
  auto recipe = readyRecipe();
  recipe.faces[0].material.textured = true;
  const auto plan = spyro::world_scene_submitter::prepare(
      harness.core.get(), harness.game->rq, 0x800258f0u, recipe);
  CHECK(plan.status == spyro::world_scene_submitter::Status::InvalidOrder);
  CHECK_EQ(plan.paintOrder.size(), 0u);
  CHECK_EQ(harness.game->rq.n, 0);
}

void test_capacity_refusal_is_atomic() {
  Harness harness;
  harness.game->rq.consumed = 0;
  harness.game->rq.n = RQ_MAX;
  const auto plan = spyro::world_scene_submitter::prepare(
      harness.core.get(), harness.game->rq, 0x800258f0u, readyRecipe());
  CHECK(plan.status == spyro::world_scene_submitter::Status::QueueCapacityExceeded);
  CHECK_EQ(harness.game->rq.n, RQ_MAX);
}

} // namespace

int main() {
  RUN(ready_recipe_preflights_without_mutation);
  RUN(invalid_material_refuses_atomically);
  RUN(capacity_refusal_is_atomic);
  return pt_summary();
}
