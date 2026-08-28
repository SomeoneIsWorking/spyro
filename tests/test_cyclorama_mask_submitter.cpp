#include "core.h"
#include "cyclorama_mask_submitter.h"
#include "game.h"
#include "render_queue.h"
#include "testutil.h"

#include <array>
#include <memory>

namespace {

struct Harness {
  std::unique_ptr<Core> core = std::make_unique<Core>();
  std::unique_ptr<Game> game = std::make_unique<Game>();

  Harness() {
    core->game = game.get();
  }
};

spyro::cyclorama_mask_recipe::Recipe recipe(uint32_t ordinal) {
  spyro::cyclorama_mask_recipe::Recipe out{};
  out.status = spyro::cyclorama_mask_recipe::Status::Ready;
  out.portalOrdinal = ordinal;
  out.otBin = (uint16_t)(100u + ordinal);
  spyro::cyclorama_mask_recipe::Face face{};
  face.rgb = 0x00332211u;
  face.vertices[0] = {10, 20, 10.0f, 20.0f};
  face.vertices[1] = {20, 20, 20.0f, 20.0f};
  face.vertices[2] = {20, 30, 20.0f, 30.0f};
  out.faces.push_back(face);
  return out;
}

void test_batch_admission_and_submission_preserve_order() {
  Harness h;
  auto first = recipe(0);
  auto second = recipe(1);
  const std::array<spyro::cyclorama_mask_submitter::Draw, 2> draws{{{&first}, {&second}}};

  const auto plan = spyro::cyclorama_mask_submitter::prepare(h.core.get(), h.game->rq, draws);
  CHECK(plan.status == spyro::cyclorama_mask_submitter::Status::Ready);
  CHECK_EQ(plan.faces.size(), 2u);
  CHECK(plan.faces[0].replay.authored());
  CHECK(plan.faces[0].replay.key.ot_bin == first.otBin);
  CHECK(plan.faces[0].replay.key.link_ordinal > plan.faces[1].replay.key.link_ordinal);
  CHECK_EQ(h.game->rq.n, 0);

  spyro::cyclorama_mask_submitter::submit(h.core.get(), h.game->rq, draws, plan);
  CHECK_EQ(h.game->rq.n, 2);
  CHECK_EQ(h.game->rq.items[0].painter_object, spyro::cyclorama_mask_recipe::kProducerKey);
  CHECK_EQ(h.game->rq.items[1].painter_object, spyro::cyclorama_mask_recipe::kProducerKey);
  CHECK_EQ(h.game->rq.items[0].nv, 3);
  CHECK_EQ(h.game->rq.items[0].mode, 3);
}

void test_capacity_refusal_is_atomic() {
  Harness h;
  h.game->rq.consumed = 0;
  h.game->rq.n = RQ_MAX;
  auto mask = recipe(0);
  const std::array<spyro::cyclorama_mask_submitter::Draw, 1> draws{{{&mask}}};
  const auto plan = spyro::cyclorama_mask_submitter::prepare(h.core.get(), h.game->rq, draws);
  CHECK(plan.status == spyro::cyclorama_mask_submitter::Status::QueueCapacityExceeded);
  CHECK(plan.faces.empty());
  CHECK_EQ(h.game->rq.n, RQ_MAX);
}

} // namespace

int main() {
  RUN(batch_admission_and_submission_preserve_order);
  RUN(capacity_refusal_is_atomic);
  return pt_summary();
}
