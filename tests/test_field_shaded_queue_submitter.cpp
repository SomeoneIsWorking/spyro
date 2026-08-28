#include "field_shaded_queue_submitter.h"
#include "render_queue.h"
#include "testutil.h"

#include <memory>

namespace {

spyro::field_shaded_queue_recipe::Recipe readyRecipe() {
  spyro::field_shaded_queue_recipe::Recipe recipe{};
  recipe.status = spyro::field_shaded_queue_recipe::Status::Ready;
  recipe.faces.push_back({.paintGroup = 0, .otBin = 12, .vertexCount = 3});
  return recipe;
}

void test_complete_recipe_preflights_without_mutation() {
  auto queue = std::make_unique<RenderQueue>();
  const auto plan =
      spyro::field_shaded_queue_submitter::prepare(*queue, 0x80022a2cu, readyRecipe());
  CHECK(plan.status == spyro::field_shaded_queue_submitter::Status::Ready);
  CHECK_EQ(queue->n, 0);
}

void test_invalid_order_refuses_atomically() {
  auto queue = std::make_unique<RenderQueue>();
  auto recipe = readyRecipe();
  recipe.faces[0].paintGroup = 1u << 30u;
  const auto plan = spyro::field_shaded_queue_submitter::prepare(*queue, 0x80022a2cu, recipe);
  CHECK(plan.status == spyro::field_shaded_queue_submitter::Status::InvalidOrder);
  CHECK_EQ(queue->n, 0);
}

void test_capacity_refusal_is_atomic() {
  auto queue = std::make_unique<RenderQueue>();
  queue->consumed = 0;
  queue->n = RQ_MAX;
  const auto plan =
      spyro::field_shaded_queue_submitter::prepare(*queue, 0x80022a2cu, readyRecipe());
  CHECK(plan.status == spyro::field_shaded_queue_submitter::Status::QueueCapacityExceeded);
  CHECK_EQ(queue->n, RQ_MAX);
}

} // namespace

int main() {
  RUN(complete_recipe_preflights_without_mutation);
  RUN(invalid_order_refuses_atomically);
  RUN(capacity_refusal_is_atomic);
  return pt_summary();
}
