#include "actor_face_submitter.h"
#include "render_queue.h"
#include "testutil.h"

#include <array>
#include <memory>

namespace {

spyro::actor_prefix::Output triangle_record() {
  spyro::actor_prefix::Output output{};
  output.status = spyro::actor_prefix::Status::Ok;
  output.otShift = 4;
  output.controls[13] = 0;
  output.controls[14] = 8;
  output.primitiveWords = {0x00002020u, 0u};
  output.colors = {0x00112233u};
  output.vertices.resize(3);
  const std::array<std::array<int16_t, 2>, 3> xy{{{{0, 0}}, {{10, 0}}, {{0, 10}}}};
  for (uint32_t i = 0; i < 3; ++i) {
    output.vertices[i].projected.sx = xy[i][0];
    output.vertices[i].projected.sy = xy[i][1];
    output.vertices[i].projected.sz = 1000;
    output.vertices[i].scratchWord = (uint16_t)xy[i][0] | ((uint32_t)(uint16_t)xy[i][1] << 16);
  }
  return output;
}

void test_complete_call_is_preflighted_without_queue_mutation() {
  auto queue = std::make_unique<RenderQueue>();
  const auto output = triangle_record();
  const auto recipe = spyro::actor_draw_recipe::compose(std::span(&output, 1));
  const auto plan = spyro::actor_face_submitter::prepare(
      *queue, 0x80020f34u, std::span(&output, 1), recipe.faces);
  CHECK(plan.status == spyro::actor_face_submitter::Status::Ready);
  CHECK_EQ(plan.replay.size(), 1u);
  CHECK_EQ(plan.materials.size(), 1u);
  CHECK(!plan.materials[0].textured);
  CHECK_EQ(queue->n, 0);
}

void test_invalid_order_refuses_atomically() {
  auto queue = std::make_unique<RenderQueue>();
  const auto output = triangle_record();
  auto recipe = spyro::actor_draw_recipe::compose(std::span(&output, 1));
  recipe.faces[0].record = 1;
  const auto plan = spyro::actor_face_submitter::prepare(
      *queue, 0x80020f34u, std::span(&output, 1), recipe.faces);
  CHECK(plan.status == spyro::actor_face_submitter::Status::InvalidGlobalOrder);
  CHECK_EQ(plan.replay.size(), 0u);
  CHECK_EQ(plan.materials.size(), 0u);
  CHECK_EQ(queue->n, 0);
}

void test_capacity_refusal_clears_the_plan() {
  auto queue = std::make_unique<RenderQueue>();
  queue->consumed = 0;
  queue->n = RQ_MAX;
  const auto output = triangle_record();
  const auto recipe = spyro::actor_draw_recipe::compose(std::span(&output, 1));
  const auto plan = spyro::actor_face_submitter::prepare(
      *queue, 0x80020f34u, std::span(&output, 1), recipe.faces);
  CHECK(plan.status == spyro::actor_face_submitter::Status::QueueCapacityExceeded);
  CHECK_EQ(plan.replay.size(), 0u);
  CHECK_EQ(plan.materials.size(), 0u);
  CHECK_EQ(queue->n, RQ_MAX);
}

} // namespace

int main() {
  RUN(complete_call_is_preflighted_without_queue_mutation);
  RUN(invalid_order_refuses_atomically);
  RUN(capacity_refusal_clears_the_plan);
  return pt_summary();
}
